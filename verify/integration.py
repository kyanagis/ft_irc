#!/usr/bin/env python3
# ircserv 結合スモークテスト（提出物のビルドとは独立・採点対象外）
#
# 現段階（コマンド未実装・Dispatcherのみ）で green にできる範囲を検査する:
#   - N14 : 部分送信（1コマンドを複数パケットに分割）を1行に再構築して1応答を返す
#   - N8  : 未知/不正コマンド連打・長すぎる行でもサーバが落ちない
#
# 登録フロー(001 RPL_WELCOME)は PASS/NICK/USER 実装後に
# 環境変数 IRC_TEST_REGISTRATION=1 を立てて有効化する。

import contextlib
import os
import socket
import subprocess
import sys
import time

HOST = "127.0.0.1"
PASSWORD = "cipass"

_failures = []


def check(name, cond, detail=""):
    print(("  ok   " if cond else "  FAIL ") + name + ("" if cond else "  :: " + detail))
    if not cond:
        _failures.append(name)


def free_port():
    s = socket.socket()
    s.bind((HOST, 0))
    port = s.getsockname()[1]
    s.close()
    return port


def start_server(port):
    proc = subprocess.Popen(
        ["./ircserv", str(port), PASSWORD],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )
    for _ in range(50):
        if proc.poll() is not None:
            out = proc.stdout.read().decode(errors="replace") if proc.stdout else ""
            raise RuntimeError("server exited early:\n" + out)
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


def test_registration(port):
    # PASS/NICK/USER 実装後のみ有効（IRC_TEST_REGISTRATION=1）
    s = socket.create_connection((HOST, port), timeout=2)
    s.sendall(b"PASS " + PASSWORD.encode() + b"\r\nNICK alice\r\nUSER a 0 * :Alice\r\n")
    s.settimeout(2.0)
    buf = b""
    with contextlib.suppress(socket.timeout):
        while b"001" not in buf:
            chunk = s.recv(4096)
            if not chunk:
                break
            buf += chunk
    s.close()
    check("registration returns 001 RPL_WELCOME", b"001" in buf, repr(buf))


def main():
    port = free_port()
    proc = start_server(port)
    try:
        test_partial_send(port)
        test_no_crash(port)
        if os.environ.get("IRC_TEST_REGISTRATION") == "1":
            test_registration(port)
        else:
            print("  skip  registration flow "
                  "(PASS/NICK/USER 未実装: IRC_TEST_REGISTRATION=1 で有効化)")
    finally:
        proc.terminate()
        with contextlib.suppress(Exception):
            proc.wait(timeout=3)

    if _failures:
        print("integration FAILED: " + ", ".join(_failures))
        sys.exit(1)
    print("integration OK")


if __name__ == "__main__":
    main()
