#!/usr/bin/env python3
# ircserv 結合スモークテスト（提出物のビルドとは独立・採点対象外）
#
# 検査内容:
#   - N14 : 部分送信（1コマンドを複数パケットに分割）を1行に再構築して1応答を返す
#   - N8  : 未知/不正コマンド連打・長すぎる行でもサーバが落ちない
#   - 登録フロー(001 RPL_WELCOME) と登録系エラー(433/464/461)  ※IRC_TEST_REGISTRATION=1
#   - 全コマンドの E2E（PRIVMSG/NOTICE/TOPIC/MODE/KICK/INVITE/PING/QUIT）
#
#   全コマンドのテストを先行実装するが、未実装コマンドで CI を落とさないための方式:
#   各コマンドテストは実行前に capability probe を行う。Dispatcher は未登録コマンドに
#   必ず `421 <CMD> :Unknown command` を返す（CommandDispatcher.cpp）。probe が 421 を
#   見たら「未実装」と判断して fail ではなく skip する。コマンドが Dispatcher に結線
#   された瞬間に自動で有効化され、以後はずっと回帰ガードとして走る。PR タイトルや
#   環境変数には一切依存しない（push:[main] でも同じ挙動）。
#
# サニタイズ版サーバ（verify/ircserv_asan）を IRCSERV_BIN で指すと、ASan/UBSan 下で
# 上記ワークロードを流し、動作中のメモリ破壊・未定義動作を検出する。

import contextlib
import os
import socket
import subprocess
import sys
import time

HOST = "127.0.0.1"
PASSWORD = "cipass"

# サーバの stdout/stderr を退避するファイル（CI が失敗時にアーティファクト回収する）
LOG_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "server.log")

# 既定は採点対象バイナリ。CI は IRCSERV_BIN=verify/ircserv_asan を渡してサニタイズ版を回す。
SERVER_BIN = os.environ.get("IRCSERV_BIN", "./ircserv")

_failures = []
_skipped = []
_log_file = None
_nick_seq = [0]


def check(name, cond, detail=""):
    print(("  ok   " if cond else "  FAIL ") + name + ("" if cond else "  :: " + detail))
    if not cond:
        _failures.append(name)


def skip(name, reason):
    print("  skip  " + name + "  :: " + reason)
    _skipped.append(name)


def unique_nick():
    _nick_seq[0] += 1
    return ("u%d" % _nick_seq[0]).encode()


def free_port():
    s = socket.socket()
    s.bind((HOST, 0))
    port = s.getsockname()[1]
    s.close()
    return port


def _read_log():
    with contextlib.suppress(OSError):
        with open(LOG_PATH, "rb") as f:
            return f.read().decode(errors="replace")
    return ""


def start_server(port):
    # PIPE を read しないとバッファ詰まりで固まりうるので、常にファイルへ流す。
    # 失敗時はこのファイルを CI がアップロードして原因追跡に使う。
    global _log_file
    _log_file = open(LOG_PATH, "wb")
    proc = subprocess.Popen(
        [SERVER_BIN, str(port), PASSWORD],
        stdout=_log_file, stderr=subprocess.STDOUT,
    )
    for _ in range(50):
        if proc.poll() is not None:
            _log_file.flush()
            raise RuntimeError("server exited early:\n" + _read_log())
        with contextlib.suppress(OSError):
            c = socket.create_connection((HOST, port), timeout=0.2)
            c.close()
            return proc
        time.sleep(0.1)
    raise RuntimeError("server did not start listening")


def recv_until_crlf(sock, timeout=2.0):
    sock.settimeout(timeout)
    buf = b""
    with contextlib.suppress(socket.timeout):
        while b"\r\n" not in buf:
            chunk = sock.recv(4096)
            if not chunk:
                break
            buf += chunk
    return buf


def recv_until(sock, needle, timeout=2.0):
    # needle が現れるまで（または timeout / EOF まで）読む。応答の特定行を待つのに使う。
    sock.settimeout(timeout)
    buf = b""
    with contextlib.suppress(socket.timeout):
        while needle not in buf:
            chunk = sock.recv(4096)
            if not chunk:
                break
            buf += chunk
    return buf


def register(port, nick=None, user=None):
    # PASS+NICK+USER で登録を完了させ、ウェルカム(001-004)を読み切ってから返す。
    if nick is None:
        nick = unique_nick()
    if user is None:
        user = nick
    s = socket.create_connection((HOST, port), timeout=3)
    s.sendall(
        b"PASS " + PASSWORD.encode() + b"\r\n"
        + b"NICK " + nick + b"\r\n"
        + b"USER " + user + b" 0 * :Real " + nick + b"\r\n"
    )
    welcome = recv_until(s, b"004", timeout=3.0)
    welcome += recv_until(s, b"\xff\xff", timeout=0.3)  # ウェルカム残りを掃き出す
    return s, nick, welcome


def implemented(port, cmd):
    # Dispatcher が `421 <CMD>` を返さなければ「実装済み（結線済み）」と判断する。
    # 未実装は 421 が即返るので速い。実装済みで無応答のコマンドは 1 秒だけ待つ。
    s, _, _ = register(port)
    s.sendall(cmd + b"\r\n")
    resp = recv_until(s, b"421", timeout=1.0)
    with contextlib.suppress(OSError):
        s.close()
    return not (b"421" in resp and cmd in resp)


# ----------------------------------------------------------------- N14 / N8

def test_partial_send(port):
    # 'FOO\r\n' を3分割で送る → 再構築されて 421 が1回だけ返る（N14）
    s = socket.create_connection((HOST, port), timeout=2)
    for frag in (b"FO", b"O", b"\r\n"):
        s.sendall(frag)
        time.sleep(0.1)
    resp = recv_until_crlf(s)
    s.close()
    check(
        "N14 partial send reassembled into one command",
        resp.endswith(b"\r\n") and b"421" in resp and b"FOO" in resp,
        repr(resp),
    )


def test_no_crash(port):
    # 不正/未知コマンド連打＋長すぎる行。落ちずに新規接続を受けられること（N8）
    s = socket.create_connection((HOST, port), timeout=2)
    s.sendall(b"@@@\r\nNOPE x y\r\n" + b"A" * 2000 + b"\r\n")
    recv_until_crlf(s)
    s.close()

    s2 = socket.create_connection((HOST, port), timeout=2)
    s2.sendall(b"BAR\r\n")
    resp = recv_until_crlf(s2)
    s2.close()
    check(
        "N8 server stays alive after garbage/over-long input",
        b"421" in resp and resp.endswith(b"\r\n"),
        repr(resp),
    )


# ----------------------------------------------- 登録フロー（実装済み・回帰ガード）

def test_registration(port):
    s = socket.create_connection((HOST, port), timeout=2)
    s.sendall(b"PASS " + PASSWORD.encode() + b"\r\nNICK alice\r\nUSER a 0 * :Alice\r\n")
    buf = recv_until(s, b"001", timeout=2.0)
    s.close()
    check("registration returns 001 RPL_WELCOME", b"001" in buf, repr(buf))


def test_nick_in_use(port):
    a, an, _ = register(port)                       # an を占有
    s = socket.create_connection((HOST, port), timeout=2)
    s.sendall(b"PASS " + PASSWORD.encode() + b"\r\nNICK " + an + b"\r\n")
    r = recv_until(s, b"433", timeout=1.5)
    check("duplicate NICK -> 433", b"433" in r, repr(r))
    s.close()
    a.close()


def test_bad_pass(port):
    s = socket.create_connection((HOST, port), timeout=2)
    s.sendall(b"PASS wrongpass\r\nNICK zz\r\nUSER zz 0 * :z\r\n")
    r = recv_until(s, b"464", timeout=1.5)
    check("wrong PASS -> 464", b"464" in r, repr(r))
    s.close()


def test_not_enough_params(port):
    # PART は実装済み。引数不足で 461 が返ることを確認（461 経路の回帰ガード）。
    s, _, _ = register(port)
    s.sendall(b"PART\r\n")
    r = recv_until(s, b"461", timeout=1.5)
    check("PART without params -> 461", b"461" in r, repr(r))
    s.close()


# --------------------------------------- 全コマンド E2E（未実装は 421 検出で自動 skip）

def test_privmsg(port):
    if not implemented(port, b"PRIVMSG"):
        return skip("PRIVMSG", "not implemented (Dispatcher returns 421)")
    a, an, _ = register(port)
    b, bn, _ = register(port)
    # nick 宛: 宛先に届き、送信者には返らない
    a.sendall(b"PRIVMSG " + bn + b" :hi-nick\r\n")
    resp = recv_until(b, b"hi-nick")
    check("PRIVMSG nick delivered to target",
          b"PRIVMSG" in resp and bn in resp and b"hi-nick" in resp
          and (b":" + an + b"!") in resp, repr(resp))
    echo = recv_until(a, b"hi-nick", timeout=0.4)
    check("PRIVMSG not echoed back to sender", b"hi-nick" not in echo, repr(echo))
    # channel 宛: メンバに届く
    a.sendall(b"JOIN #pm\r\n"); recv_until(a, b"366")
    b.sendall(b"JOIN #pm\r\n"); recv_until(b, b"366")
    recv_until(a, b"JOIN", timeout=0.4)                 # bob の JOIN broadcast を掃き出す
    a.sendall(b"PRIVMSG #pm :hi-chan\r\n")
    resp2 = recv_until(b, b"hi-chan")
    check("PRIVMSG channel delivered to member",
          b"PRIVMSG" in resp2 and b"#pm" in resp2 and b"hi-chan" in resp2, repr(resp2))
    a.close()
    b.close()


def test_notice(port):
    if not implemented(port, b"NOTICE"):
        return skip("NOTICE", "not implemented (Dispatcher returns 421)")
    a, an, _ = register(port)
    b, bn, _ = register(port)
    a.sendall(b"NOTICE " + bn + b" :hey-notice\r\n")
    resp = recv_until(b, b"hey-notice")
    check("NOTICE delivered to target",
          b"NOTICE" in resp and b"hey-notice" in resp and (b":" + an + b"!") in resp,
          repr(resp))
    # 決定的な違い: 不明ターゲットでもエラー numeric を一切返さない（RFC 2812 §3.3.2）
    a.sendall(b"NOTICE nosuchnick_zzz :x\r\n")
    err = recv_until(a, b"401", timeout=0.5)
    check("NOTICE never returns error numeric", b"401" not in err, repr(err))
    a.close()
    b.close()


def test_topic(port):
    if not implemented(port, b"TOPIC"):
        return skip("TOPIC", "not implemented (Dispatcher returns 421)")
    a, an, _ = register(port)
    a.sendall(b"JOIN #tp\r\n"); recv_until(a, b"366")
    a.sendall(b"TOPIC #tp\r\n")
    q = recv_until(a, b"331")
    check("TOPIC query on empty -> 331", b"331" in q, repr(q))
    a.sendall(b"TOPIC #tp :hello world\r\n")
    setr = recv_until(a, b"hello world")
    check("TOPIC set broadcasts to channel",
          b"TOPIC" in setr and b"#tp" in setr and b"hello world" in setr
          and (b":" + an + b"!") in setr, repr(setr))
    a.sendall(b"TOPIC #tp\r\n")
    q2 = recv_until(a, b"332")
    check("TOPIC query after set -> 332", b"332" in q2 and b"hello world" in q2, repr(q2))
    a.close()


def test_mode(port):
    if not implemented(port, b"MODE"):
        return skip("MODE", "not implemented (Dispatcher returns 421)")
    a, an, _ = register(port)
    a.sendall(b"JOIN #md\r\n"); recv_until(a, b"366")
    a.sendall(b"MODE #md\r\n")
    q = recv_until(a, b"324")
    check("MODE query -> 324 RPL_CHANNELMODEIS", b"324" in q and b"#md" in q, repr(q))
    a.sendall(b"MODE #md +i\r\n")
    r = recv_until(a, b"+i")
    check("MODE +i broadcast",
          b"MODE" in r and b"#md" in r and b"+i" in r and (b":" + an + b"!") in r, repr(r))
    a.sendall(b"MODE #md +k secretkey\r\n")
    r2 = recv_until(a, b"secretkey")
    check("MODE +k broadcast carries key", b"+k" in r2 and b"secretkey" in r2, repr(r2))
    a.close()


def test_kick(port):
    if not implemented(port, b"KICK"):
        return skip("KICK", "not implemented (Dispatcher returns 421)")
    a, an, _ = register(port)
    b, bn, _ = register(port)
    a.sendall(b"JOIN #kk\r\n"); recv_until(a, b"366")
    b.sendall(b"JOIN #kk\r\n"); recv_until(b, b"366")
    recv_until(a, b"JOIN", timeout=0.4)
    a.sendall(b"KICK #kk " + bn + b" :bye\r\n")
    r = recv_until(a, b"KICK")
    check("KICK broadcasts to channel",
          b"KICK" in r and b"#kk" in r and bn in r and b"bye" in r
          and (b":" + an + b"!") in r, repr(r))
    rb = recv_until(b, b"KICK")
    check("KICK notifies the target", b"KICK" in rb and bn in rb, repr(rb))
    a.close()
    b.close()


def test_invite(port):
    if not implemented(port, b"INVITE"):
        return skip("INVITE", "not implemented (Dispatcher returns 421)")
    a, an, _ = register(port)
    b, bn, _ = register(port)
    a.sendall(b"JOIN #iv\r\n"); recv_until(a, b"366")
    a.sendall(b"INVITE " + bn + b" #iv\r\n")
    r = recv_until(a, b"341")
    check("INVITE -> 341 RPL_INVITING to inviter",
          b"341" in r and bn in r and b"#iv" in r, repr(r))
    rb = recv_until(b, b"INVITE")
    check("INVITE relayed to the target",
          b"INVITE" in rb and b"#iv" in rb and (b":" + an + b"!") in rb, repr(rb))
    a.close()
    b.close()


def test_ping(port):
    if not implemented(port, b"PING"):
        return skip("PING", "not implemented (Dispatcher returns 421)")
    s, _, _ = register(port)
    s.sendall(b"PING :tok-12345\r\n")
    r = recv_until(s, b"tok-12345")
    check("PING -> PONG carries the same token", b"PONG" in r and b"tok-12345" in r, repr(r))
    s.close()


def test_quit(port):
    if not implemented(port, b"QUIT"):
        return skip("QUIT", "not implemented (Dispatcher returns 421)")
    a, an, _ = register(port)
    b, _, _ = register(port)
    a.sendall(b"JOIN #qt\r\n"); recv_until(a, b"366")
    b.sendall(b"JOIN #qt\r\n"); recv_until(b, b"366")
    recv_until(a, b"JOIN", timeout=0.4)
    a.sendall(b"QUIT :gone-now\r\n")
    r = recv_until(b, b"QUIT")
    check("QUIT notifies channel members",
          b"QUIT" in r and b"gone-now" in r and (b":" + an + b"!") in r, repr(r))
    with contextlib.suppress(OSError):
        a.close()
    b.close()


def check_server_log(log):
    # サーバ側ターミナル出力（起動バナー＋イベントログ）の回帰ガード。
    # ここまでのテストが接続/登録/チャンネル生成/破棄/拒否を必ず踏んでいるので、
    # 各カテゴリの行がログに現れることを機械検査する。
    check("startup banner printed",
          "ircserv 1.0" in log and "listening on 0.0.0.0:" in log,
          repr(log[:200]))
    check("log records accepted connections", "CONN" in log and "clients:" in log)
    check("log records registration", "AUTH" in log and "registered" in log)
    check("log records channel creation", "created by" in log)
    check("log records channel destruction", "destroyed" in log)
    check("log records membership changes", "MEMB" in log)
    check("log records rejected commands", "DENY" in log)
    check("log prints shutdown summary",
          "shutting down" in log and "uptime" in log)


def main():
    port = free_port()
    proc = start_server(port)
    server_died_rc = None
    try:
        test_partial_send(port)
        test_no_crash(port)

        if os.environ.get("IRC_TEST_REGISTRATION") == "1":
            test_registration(port)
            test_nick_in_use(port)
            test_bad_pass(port)
            test_not_enough_params(port)
        else:
            print("  skip  registration flow "
                  "(PASS/NICK/USER 未実装: IRC_TEST_REGISTRATION=1 で有効化)")

        # 全コマンド E2E。未実装のものは各関数内の probe で自動 skip される。
        test_privmsg(port)
        test_notice(port)
        test_topic(port)
        test_mode(port)
        test_kick(port)
        test_invite(port)
        test_ping(port)
        test_quit(port)
    finally:
        server_died_rc = proc.poll()      # テスト中に死んでいれば非 None（crash/サニタイザ abort）
        proc.terminate()                  # SIGTERM → サーバは _running=0 で run() を抜け main を正常 return
        exit_rc = None
        with contextlib.suppress(Exception):
            exit_rc = proc.wait(timeout=5)
        if exit_rc is None:               # 時間内に終了しない = clean shutdown 失敗。強制終了して記録
            with contextlib.suppress(Exception):
                proc.kill()
                proc.wait(timeout=3)
        if _log_file is not None:
            with contextlib.suppress(Exception):
                _log_file.close()

    # サニタイザ検出: サーバがテスト中に異常終了 or ログにサニタイザ報告があれば失敗。
    # 動作中の heap-overflow / use-after-free / UB は即 abort してここで赤になる。
    log = _read_log()
    markers = ("runtime error:", "AddressSanitizer",
               "UndefinedBehaviorSanitizer", "LeakSanitizer")
    if server_died_rc is not None:
        check("server stayed alive during tests (no crash/sanitizer abort)",
              False, "server exited early rc=%d" % server_died_rc)
    hit = [m for m in markers if m in log]
    check("no sanitizer report in server log", not hit, "markers=%r" % hit)

    # クリーン終了の検証: SIGTERM で run() を抜け main が 0 を返すこと。
    # これが (1) leak 検出（LSan は正常終了時のみ動く）と
    #        (2) サーバ側カバレッジ（profraw は正常終了時に書き出される）を有効化する。
    # LSan が leak を報告すると exit_rc != 0 になり、このチェックが赤にする。
    check("server shuts down cleanly on SIGTERM (rc==0; enables LSan leak check + profraw)",
          exit_rc == 0, "exit_rc=%r" % exit_rc)

    if os.environ.get("IRC_TEST_REGISTRATION") == "1":
        check_server_log(log)

    if _skipped:
        print("skipped (unimplemented): " + ", ".join(_skipped))
    if _failures:
        print("integration FAILED: " + ", ".join(_failures))
        print("server log -> " + LOG_PATH)
        sys.exit(1)
    print("integration OK")


if __name__ == "__main__":
    main()
