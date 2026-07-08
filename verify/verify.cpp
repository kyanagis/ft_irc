// ft_irc 検証ハーネス（提出物のビルドとは独立・採点対象外）
//
// 目的: 採点対象の src/ の「純粋ロジック」を、実コードそのものに対して
//       有界全数 + プロパティ + N-version差分 + サニタイザで機械検査する。
//       poll ループ・socket・状態遷移の I/O 層は verify/integration.py が担当。
//
// 検証内容（この1バイナリが検査する全項目）:
//   [Message パーサ]  ../src/Message.cpp
//     - 実装 ≡ 実行可能仕様(spec_parser.hpp) を縮約アルファベット長さ<=12 全数で証明
//     - 任意バイト列(0x00-0xFF, CR/LF含む)での差分＋健全性不変条件
//     - ラウンドトリップ parse(serialize(m)) == normalize(m)
//     - 文字クラス抽象化補題（置換σとの可換性）← 全長への一般化の根拠
//     - 範囲外 param(i) が常に空（メモリ安全）
//   [Reply 整形]  ../src/Reply.cpp
//     - numeric() をコード0..999全数×target/rest代表で独立実装と一致
//     - RFC形式ゴールデンベクタ / from() / 数値定数が定義域[1,999]
//   [StringUtil]  ../src/StringUtil.cpp
//     - toUpper/toLower/trim/split/toString の代表・境界ケース
//   [Client]  ../src/Client.cpp
//     - extractLine: CR-LF/LF/空行/部分受信/埋め込みCR の行再構築（N14の核）
//     - 登録ステートマシン(pass/nick/user/registered)・prefix()・送受信バッファ・overflow
//   [Channel]  ../src/Channel/*.cpp
//     - 生成時の creator=member+operator、membership/operator/invite、topic
//     - モード i/t/k/l と modeString() の厳密な整形
//   [IrcException]  ../src/IrcException.cpp
//     - code/target/detail/what() の一致
//   [横断] 上記すべてを ASan+UBSan 下で実行 → メモリ安全・未定義動作なしの証拠
//
// 有界全数の“証明”について（正直な注記）:
//   長さ<=12 の全列挙は、その有限領域では実装≡仕様の演繹的証明。全長への一般化は
//   「パーサが先頭からの1パス走査で長さ依存の状態を持たない（13文字目以降に新しい
//   制御パスが現れない）」ことと置換補題(T2)による帰納的確信であり、演繹証明ではない。
//
// 使い方:
//   ./verify all [maxLen]   … 全項目（T1は長さ<=maxLen 全数、既定12）
//   ./verify exhaustive N   … Message実装≡仕様（縮約アルファベット長さ<=N 全数）
//   ./verify garbage ITERS  … ランダム任意バイト列で差分＋不変条件
//   ./verify roundtrip ITERS… parse(serialize(m)) == normalize(m)
//   ./verify subst ITERS    … 文字クラス抽象化補題（σとの可換性）
//   ./verify reply          … Reply全数＋ゴールデンベクタ
//   ./verify units          … StringUtil / Client / Channel / IrcException
//
// 乱数は固定シードのxorshift（再現可能）。ASan/UBSan付きでビルドすること。

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "Channel.hpp"
#include "Client.hpp"
#include "IrcException.hpp"
#include "Message.hpp"
#include "Reply.hpp"
#include "StringUtil.hpp"
#include "spec_parser.hpp"

static unsigned long g_checks = 0;
static unsigned long g_failures = 0;

// ---------------------------------------------------------------- utilities

// - 再現可能な xorshift 乱数（固定シードで回す）
struct Rng {
	unsigned long s;
	explicit Rng(unsigned long seed) : s(seed | 1UL) {}
	unsigned long next() {
		s ^= (s << 13) & 0xffffffffUL;
		s ^= (s >> 17);
		s ^= (s << 5) & 0xffffffffUL;
		return s & 0xffffffffUL;
	}
	unsigned long below(unsigned long n) { return next() % n; }
};

// - ASCII大文字化（Message::parse の command 正規化と突き合わせる独立実装）
static std::string upperAscii(const std::string& s) {
	std::string r(s);
	for (std::string::size_type i = 0; i < r.size(); ++i) {
		r[i] = static_cast<char>(
				std::toupper(static_cast<unsigned char>(r[i])));
	}
	return r;
}

// - 失敗ログ用に非印字バイトを \xNN で見えるようにする
static std::string quoted(const std::string& s) {
	std::string r = "[";
	for (std::string::size_type i = 0; i < s.size(); ++i) {
		unsigned char c = static_cast<unsigned char>(s[i]);
		if (c >= 32 && c < 127) {
			r += static_cast<char>(c);
		} else {
			static const char* hex = "0123456789abcdef";
			r += "\\x";
			r += hex[c >> 4];
			r += hex[c & 0x0f];
		}
	}
	return r + "]";
}

// - 差分/不変条件の失敗を記録
static void fail(const std::string& what, const std::string& input) {
	++g_failures;
	std::fprintf(stderr, "FAIL %s input=%s\n", what.c_str(), quoted(input).c_str());
}

// - 単純な真偽アサート（ユニット検査用）
static void expect(const char* what, bool cond) {
	++g_checks;
	if (!cond) {
		++g_failures;
		std::fprintf(stderr, "FAIL %s\n", what);
	}
}

// - 文字列一致アサート（不一致は got/want を表示）
static void checkEq(const std::string& actual, const std::string& expected,
		const std::string& what) {
	++g_checks;
	if (actual != expected) {
		++g_failures;
		std::fprintf(stderr, "FAIL %s\n  got  %s\n  want %s\n",
				what.c_str(), quoted(actual).c_str(), quoted(expected).c_str());
	}
}

// ------------------------------------------------- T1: 実装 ≡ 仕様（差分検査）

// - 1行について Message::parse と specParse の全成分一致を検査
static void checkAgainstSpec(const std::string& line) {
	++g_checks;
	Message impl = Message::parse(line);
	SpecMessage spec = specParse(line);

	if (impl.empty() != spec.isEmpty) { fail("empty() mismatch", line); return; }
	if (impl.prefix() != spec.prefix) { fail("prefix mismatch", line); return; }
	if (impl.command() != spec.command) { fail("command mismatch", line); return; }
	if (impl.size() != spec.params.size()) { fail("param count mismatch", line); return; }
	for (std::size_t i = 0; i < spec.params.size(); ++i) {
		if (impl.param(i) != spec.params[i]) { fail("param value mismatch", line); return; }
	}
	if (impl.params() != spec.params) { fail("params() mismatch", line); return; }
	if (impl.param(impl.size()) != "") { fail("param() OOB not empty", line); return; }
	if (impl.param(1000000) != "") { fail("param() far-OOB not empty", line); return; }
}

// - T4: 任意入力に対する出力の整形式性（健全性不変条件）
static void checkInvariants(const std::string& line) {
	++g_checks;
	Message m = Message::parse(line);

	if (m.empty() != m.command().empty()) fail("INV empty<=>no-command", line);

	const std::string& cmd = m.command();
	for (std::string::size_type i = 0; i < cmd.size(); ++i) {
		char c = cmd[i];
		if (c == ' ') { fail("INV command has space", line); break; }
		if (c >= 'a' && c <= 'z') { fail("INV command not uppercased", line); break; }
	}
	const std::string& pre = m.prefix();
	if (pre.find(' ') != std::string::npos) fail("INV prefix has space", line);

	for (std::size_t i = 0; i + 1 < m.size(); ++i) {  // 最後以外のパラメータ
		const std::string& p = m.param(i);
		if (p.empty()) { fail("INV middle param empty", line); break; }
		if (p[0] == ':') { fail("INV middle param starts ':'", line); break; }
		if (p.find(' ') != std::string::npos) { fail("INV middle param has space", line); break; }
	}
	if (m.empty() && m.size() != 0) fail("INV empty msg has params", line);
}

// - T1本体: 縮約アルファベット{' ',':','a','B'}上の全文字列(長さ<=maxLen)を列挙して差分検査
//   ・パーサが区別するのは ' ' / ':' / その他 のみ（T2で正当化）→「その他」代表2文字で足りる
static void runExhaustive(int maxLen) {
	static const char ALPHA[] = { ' ', ':', 'a', 'B' };
	const int K = 4;

	unsigned long total = 0;
	for (int len = 0; len <= maxLen; ++len) {
		std::vector<int> idx(static_cast<std::size_t>(len), 0);
		std::string s(static_cast<std::size_t>(len), ALPHA[0]);
		for (;;) {
			checkAgainstSpec(s);
			checkInvariants(s);
			++total;

			int pos = len - 1;
			while (pos >= 0 && idx[static_cast<std::size_t>(pos)] == K - 1) {
				idx[static_cast<std::size_t>(pos)] = 0;
				s[static_cast<std::size_t>(pos)] = ALPHA[0];
				--pos;
			}
			if (pos < 0) break;
			++idx[static_cast<std::size_t>(pos)];
			s[static_cast<std::size_t>(pos)] = ALPHA[idx[static_cast<std::size_t>(pos)]];
		}
		std::printf("  exhaustive len=%2d done (cumulative %lu strings)\n", len, total);
	}
	std::printf("T1: exhaustive impl==spec, %lu strings, alphabet {' ',':','a','B'}, len<=%d\n",
			total, maxLen);
}

// - T1'/T4: 任意バイト列（0x00-0xff, \r \n 含む）での差分＋不変条件
static void runGarbage(unsigned long iters) {
	Rng rng(0x5EED0001UL);
	for (unsigned long it = 0; it < iters; ++it) {
		std::string s;
		unsigned long len = rng.below(40);
		for (unsigned long i = 0; i < len; ++i) {
			unsigned long r = rng.below(100);
			char c;
			if (r < 30) c = ' ';                                        // 区切りを高頻度で
			else if (r < 55) c = ':';
			else c = static_cast<char>(static_cast<unsigned char>(rng.below(256)));
			s += c;
		}
		checkAgainstSpec(s);
		checkInvariants(s);
	}
	std::printf("T1'/T4: %lu random byte strings, impl==spec + invariants\n", iters);
}

// --------------------------- T3: ラウンドトリップ parse(serialize(m)) == m

// - スペースを含まない印字トークンを生成（noLeadingColon で先頭':'を回避）
static std::string randToken(Rng& rng, unsigned long maxLen, bool noLeadingColon) {
	unsigned long len = 1 + rng.below(maxLen);
	std::string t;
	for (unsigned long i = 0; i < len; ++i) {
		char c;
		do {
			c = static_cast<char>(33 + rng.below(94));  // 印字文字（スペース以外）
		} while (i == 0 && noLeadingColon && c == ':');
		t += c;
	}
	return t;
}

// - トークン間に1〜3個のスペースを挿入（連続スペース耐性も同時に検査）
static void appendSpaces(std::string& out, Rng& rng) {
	unsigned long n = 1 + rng.below(3);
	for (unsigned long i = 0; i < n; ++i) out += ' ';
}

// - T3本体: 整形式メッセージを直列化→parseで完全復元されることを検査
static void runRoundtrip(unsigned long iters) {
	Rng rng(0x5EED0002UL);
	for (unsigned long it = 0; it < iters; ++it) {
		bool hasPrefix = rng.below(2) == 0;
		std::string prefix = hasPrefix ? randToken(rng, 10, false) : "";
		std::string cmd = randToken(rng, 8, true);

		std::vector<std::string> params;
		unsigned long nMiddle = rng.below(4);
		for (unsigned long i = 0; i < nMiddle; ++i) {
			params.push_back(randToken(rng, 8, true));
		}

		bool hasTrailing = rng.below(2) == 0;
		std::string trailing;
		if (hasTrailing) {
			unsigned long len = rng.below(15);
			for (unsigned long i = 0; i < len; ++i) {
				trailing += (rng.below(4) == 0)
						? ' '
						: static_cast<char>(33 + rng.below(94));
			}
		}

		std::string line;
		if (hasPrefix) { line += ':'; line += prefix; appendSpaces(line, rng); }
		line += cmd;
		for (std::size_t i = 0; i < params.size(); ++i) {
			appendSpaces(line, rng);
			line += params[i];
		}
		if (hasTrailing) {
			appendSpaces(line, rng);
			bool colonOptional = !trailing.empty()
					&& trailing.find(' ') == std::string::npos
					&& trailing[0] != ':';
			if (colonOptional && rng.below(2) == 0) {
				line += trailing;              // 通常パラメータ形式でも同値
			} else {
				line += ':';
				line += trailing;
			}
			params.push_back(trailing);
		}

		++g_checks;
		Message m = Message::parse(line);
		bool ok = !m.empty()
				&& m.prefix() == prefix
				&& m.command() == upperAscii(cmd)
				&& m.params() == params;
		if (!ok) fail("T3 roundtrip", line);
	}
	std::printf("T3: %lu roundtrip cases, parse(serialize(m)) == normalize(m)\n", iters);
}

// ------------- T2: 文字クラス抽象化補題（case整合な特殊文字固定置換との可換性）

// - ' ' と ':' を固定し、英字/数字を case整合に回転させる全単射σを作る
static void buildSigma(Rng& rng, char sigma[256]) {
	for (int i = 0; i < 256; ++i) sigma[i] = static_cast<char>(i);
	int rotL = static_cast<int>(rng.below(26));
	int rotD = static_cast<int>(rng.below(10));
	for (int i = 0; i < 26; ++i) {
		sigma['a' + i] = static_cast<char>('a' + (i + rotL) % 26);
		sigma['A' + i] = static_cast<char>('A' + (i + rotL) % 26);  // case整合
	}
	for (int i = 0; i < 10; ++i) {
		sigma['0' + i] = static_cast<char>('0' + (i + rotD) % 10);
	}
}

// - σを文字列へ各文字適用
static std::string applySigma(const char sigma[256], const std::string& s) {
	std::string r(s);
	for (std::string::size_type i = 0; i < r.size(); ++i) {
		r[i] = sigma[static_cast<unsigned char>(r[i])];
	}
	return r;
}

// - T2本体: parse(σ(s)) == σ̃(parse(s)) を検査（長さ12超への一般化の根拠）
static void runSubstitution(unsigned long iters) {
	static const char ALPHA[] = " ::abXYz09._!@#";
	const unsigned long K = sizeof(ALPHA) - 1;
	Rng rng(0x5EED0003UL);

	for (unsigned long it = 0; it < iters; ++it) {
		std::string s;
		unsigned long len = rng.below(30);
		for (unsigned long i = 0; i < len; ++i) s += ALPHA[rng.below(K)];

		char sigma[256];
		buildSigma(rng, sigma);

		++g_checks;
		Message a = Message::parse(s);
		Message b = Message::parse(applySigma(sigma, s));

		bool ok = b.empty() == a.empty()
				&& b.prefix() == applySigma(sigma, a.prefix())
				&& b.command() == applySigma(sigma, a.command())
				&& b.size() == a.size();
		if (ok) {
			for (std::size_t i = 0; i < a.size(); ++i) {
				if (b.param(i) != applySigma(sigma, a.param(i))) { ok = false; break; }
			}
		}
		if (!ok) fail("T2 substitution lemma", s);
	}
	std::printf("T2: %lu substitution cases, parse commutes with sigma\n", iters);
}

// ----------------------------------------------------------- T7: Reply検査

// - numeric() の期待値を iomanip 非依存の手書きゼロ埋めで独立生成
static std::string expectedNumeric(const std::string& server, int code,
		const std::string& target, const std::string& rest) {
	std::string pad(3, '0');
	pad[0] = static_cast<char>('0' + (code / 100) % 10);
	pad[1] = static_cast<char>('0' + (code / 10) % 10);
	pad[2] = static_cast<char>('0' + code % 10);
	std::string out = ":" + server + " " + pad + " "
			+ (target.empty() ? std::string("*") : target);
	if (!rest.empty()) out += " " + rest;
	return out;
}

// - T7本体: numeric全数×代表 / ゴールデンベクタ / from / 数値定数の定義域
static void runReply() {
	static const char* targets[] = { "", "alice", "*", "verylongnickname42" };
	static const char* rests[] = { "", ":Welcome", "JOIN :Not enough parameters" };
	for (int code = 0; code <= 999; ++code) {
		for (int t = 0; t < 4; ++t) {
			for (int r = 0; r < 3; ++r) {
				checkEq(Reply::numeric("ircserv", code, targets[t], rests[r]),
						expectedNumeric("ircserv", code, targets[t], rests[r]),
						"T7 numeric exhaustive");
			}
		}
	}

	checkEq(Reply::numeric("ircserv", Reply::RPL_WELCOME, "alice",
					":Welcome to the Internet Relay Network alice"),
			":ircserv 001 alice :Welcome to the Internet Relay Network alice",
			"T7 golden 001");
	checkEq(Reply::numeric("irc.example.com", Reply::ERR_NEEDMOREPARAMS, "bob",
					"JOIN :Not enough parameters"),
			":irc.example.com 461 bob JOIN :Not enough parameters",
			"T7 golden 461");
	checkEq(Reply::numeric("s", Reply::ERR_NOTREGISTERED, "",
					":You have not registered"),
			":s 451 * :You have not registered",
			"T7 golden 451 nick未確定->*");
	checkEq(Reply::numeric("s", Reply::ERR_CHANOPRIVSNEEDED, "bob",
					"#ch :You're not channel operator"),
			":s 482 bob #ch :You're not channel operator",
			"T7 golden 482");
	checkEq(Reply::from("nick!user@host", "PRIVMSG #ch :hello"),
			":nick!user@host PRIVMSG #ch :hello", "T7 golden from privmsg");
	checkEq(Reply::from("nick!user@host", "JOIN #ch"),
			":nick!user@host JOIN #ch", "T7 golden from join");

	static const int codes[] = {
		Reply::RPL_WELCOME, Reply::RPL_YOURHOST, Reply::RPL_CREATED,
		Reply::RPL_MYINFO, Reply::RPL_CHANNELMODEIS, Reply::RPL_NOTOPIC,
		Reply::RPL_TOPIC, Reply::RPL_INVITING, Reply::RPL_NAMREPLY,
		Reply::RPL_ENDOFNAMES, Reply::ERR_NOSUCHNICK, Reply::ERR_NOSUCHCHANNEL,
		Reply::ERR_CANNOTSENDTOCHAN, Reply::ERR_NORECIPIENT,
		Reply::ERR_NOTEXTTOSEND, Reply::ERR_UNKNOWNCOMMAND,
		Reply::ERR_NONICKNAMEGIVEN, Reply::ERR_ERRONEUSNICKNAME,
		Reply::ERR_NICKNAMEINUSE, Reply::ERR_USERNOTINCHANNEL,
		Reply::ERR_NOTONCHANNEL, Reply::ERR_USERONCHANNEL,
		Reply::ERR_NOTREGISTERED, Reply::ERR_NEEDMOREPARAMS,
		Reply::ERR_ALREADYREGISTRED, Reply::ERR_PASSWDMISMATCH,
		Reply::ERR_CHANNELISFULL, Reply::ERR_UNKNOWNMODE,
		Reply::ERR_INVITEONLYCHAN, Reply::ERR_BADCHANNELKEY,
		Reply::ERR_CHANOPRIVSNEEDED
	};
	for (std::size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i) {
		++g_checks;
		if (codes[i] < 1 || codes[i] > 999) {
			++g_failures;
			std::fprintf(stderr, "FAIL T7 code out of domain: %d\n", codes[i]);
		}
	}
	std::printf("T7: Reply numeric/from, 12000 exhaustive + golden vectors\n");
}

// ------------------------------------------------- StringUtil ユニット検査

// - toUpper/toLower/trim/split/toString の代表・境界（split は空要素/区切り無しも）
static void runStringUtil() {
	checkEq(StringUtil::toUpper("aB c:1"), "AB C:1", "toUpper");
	checkEq(StringUtil::toLower("aB C:1"), "ab c:1", "toLower");

	checkEq(StringUtil::trim("  x y  "), "x y", "trim both");
	checkEq(StringUtil::trim("noedge"), "noedge", "trim none");
	checkEq(StringUtil::trim("   "), "", "trim all-space");
	checkEq(StringUtil::trim("\t\r\n x \n"), "x", "trim mixed ws");

	std::vector<std::string> v = StringUtil::split("a,b,c", ',');
	expect("split count 3", v.size() == 3);
	if (v.size() == 3) {
		checkEq(v[0], "a", "split[0]");
		checkEq(v[1], "b", "split[1]");
		checkEq(v[2], "c", "split[2]");
	}
	std::vector<std::string> v2 = StringUtil::split("abc", ',');
	expect("split no-delim -> 1", v2.size() == 1 && v2[0] == "abc");
	std::vector<std::string> v3 = StringUtil::split("a,,b", ',');
	expect("split empty middle", v3.size() == 3 && v3[1] == "");
	std::vector<std::string> v4 = StringUtil::split(",x", ',');
	expect("split empty head", v4.size() == 2 && v4[0] == "" && v4[1] == "x");

	checkEq(StringUtil::toString(0), "0", "toString 0");
	checkEq(StringUtil::toString(-5), "-5", "toString neg");
	checkEq(StringUtil::toString(2147483647L), "2147483647", "toString max32");

	std::printf("units: StringUtil ok\n");
}

// ------------------------------------------------- Client ユニット検査

// - extractLine の行再構築（N14の核）を1バッファに集約して順に検証するヘルパ
static void feedAndExpectLines(const char* raw,
		const char** expected, std::size_t n) {
	Client c(-1, "h");
	c.appendInput(raw, std::string(raw).size());
	std::string line;
	for (std::size_t i = 0; i < n; ++i) {
		bool got = c.extractLine(line);
		expect("extractLine has line", got);
		if (got) checkEq(line, expected[i], "extractLine value");
	}
	expect("extractLine drained", !c.extractLine(line));
}

// - CR-LF/LF/空行/複数行/埋め込みCR/部分受信 と 登録状態機械・prefix・バッファ・overflow
static void runClient() {
	// 行再構築の各パターン
	{
		const char* e1[] = { "ABC" };
		feedAndExpectLines("ABC\r\n", e1, 1);
		const char* e2[] = { "ABC" };
		feedAndExpectLines("ABC\n", e2, 1);            // LF単独も許容
		const char* e3[] = { "A", "B" };
		feedAndExpectLines("A\r\nB\n", e3, 2);          // 複数行
		const char* e4[] = { "" };
		feedAndExpectLines("\r\n", e4, 1);              // 空行
		const char* e5[] = { "X\rY" };
		feedAndExpectLines("X\rY\n", e5, 1);            // 末尾以外のCRは保持
	}
	// 部分受信：改行が来るまで false、来たら1行に再構築（N14）
	{
		Client c(-1, "h");
		std::string line;
		c.appendInput("AB", 2);
		expect("partial: no line yet", !c.extractLine(line));
		c.appendInput("C\r\n", 3);
		expect("partial: line completes", c.extractLine(line));
		checkEq(line, "ABC", "partial reassembled");
	}
	// 登録ステートマシン
	{
		Client c(-1, "host");
		expect("fresh not pass", !c.passAccepted());
		expect("fresh not nick", !c.hasNick());
		expect("fresh not user", !c.hasUser());
		expect("fresh not registered", !c.isRegistered());
		c.acceptPass();
		expect("pass accepted", c.passAccepted());
		c.setNick("alice");
		expect("has nick", c.hasNick());
		checkEq(c.nick(), "alice", "nick value");
		c.setUser("auser", "Alice Real");
		expect("has user", c.hasUser());
		checkEq(c.user(), "auser", "user value");
		checkEq(c.realname(), "Alice Real", "realname value");
		c.markRegistered();
		expect("registered", c.isRegistered());
	}
	// prefix() の各段
	{
		Client a(1, "host");
		checkEq(a.prefix(), "*@host", "prefix nick未確定");
		a.setNick("bob");
		checkEq(a.prefix(), "bob@host", "prefix nickのみ");
		a.setUser("bu", "Bob");
		checkEq(a.prefix(), "bob!bu@host", "prefix full");
		Client b(2, "");
		checkEq(b.prefix(), "*", "prefix host無し");
	}
	// 送受信バッファ・close・overflow
	{
		Client d(4, "h");
		expect("no pending out", !d.hasPendingOutput());
		expect("not read-closed", !d.isReadClosed());
		d.appendOutput("xx");
		expect("has pending out", d.hasPendingOutput());
		checkEq(d.outBuffer(), "xx", "outBuffer");
		d.markReadClosed();
		expect("read closed", d.isReadClosed());

		Client e(5, "h");
		std::string big(600, 'A');              // 512超・改行なし
		e.appendInput(big.data(), big.size());
		expect("input overflow", e.inputOverflow());
		e.appendInput("\n", 1);                 // 改行が来れば overflow ではない
		expect("no overflow with newline", !e.inputOverflow());
	}
	std::printf("units: Client ok\n");
}

// ------------------------------------------------- Channel ユニット検査

// - 生成時 creator=member+operator、membership/operator/invite/topic、mode整形
static void runChannel() {
	Client creator(1, "h");
	Client bob(2, "h");

	Channel ch("#c", creator);
	checkEq(ch.name(), "#c", "channel name");
	expect("creator is member", ch.hasMember(creator));
	expect("creator is operator", ch.isOperator(creator));
	expect("count 1", ch.memberCount() == 1);
	expect("not empty", !ch.isEmpty());
	expect("creator joined set", creator.channels().count("#c") == 1);
	checkEq(ch.modeString(), "+", "modeString empty");

	// membership
	ch.addMember(bob);
	expect("bob member", ch.hasMember(bob));
	expect("bob not op", !ch.isOperator(bob));
	expect("count 2", ch.memberCount() == 2);
	expect("bob joined set", bob.channels().count("#c") == 1);

	// operator 付与/剥奪
	ch.addOperator(bob);
	expect("bob op", ch.isOperator(bob));
	ch.removeOperator(bob);
	expect("bob deop", !ch.isOperator(bob));

	// invite
	ch.invite(bob);
	expect("bob invited", ch.isInvited(bob));
	ch.clearInvite(bob);
	expect("bob invite cleared", !ch.isInvited(bob));

	// topic（空トピックは hasTopic=false）
	expect("no topic init", !ch.hasTopic());
	ch.setTopic("hello", "creator");
	expect("has topic", ch.hasTopic());
	checkEq(ch.topic(), "hello", "topic value");
	checkEq(ch.topicSetter(), "creator", "topic setter");
	ch.setTopic("", "creator");
	expect("empty topic clears", !ch.hasTopic());

	// modes i/t/k/l と modeString の厳密整形
	ch.setInviteOnly(true);
	ch.setTopicLocked(true);
	checkEq(ch.modeString(), "+it", "modeString +it");
	ch.setKey("secret");
	expect("has key", ch.hasKey());
	checkEq(ch.key(), "secret", "key value");
	checkEq(ch.modeString(), "+itk secret", "modeString +itk");
	ch.setLimit(5);
	expect("has limit", ch.hasLimit());
	expect("limit 5", ch.limit() == 5);
	checkEq(ch.modeString(), "+itkl secret 5", "modeString +itkl");
	ch.clearKey();
	expect("key cleared", !ch.hasKey());
	checkEq(ch.modeString(), "+itl 5", "modeString +itl");
	ch.clearLimit();
	expect("limit cleared", !ch.hasLimit());
	checkEq(ch.modeString(), "+it", "modeString back to +it");

	// removeMember は member/operator/invited/参加集合すべてを掃除する
	ch.addOperator(bob);
	ch.invite(bob);
	ch.removeMember(bob);
	expect("bob removed", !ch.hasMember(bob));
	expect("bob deop on remove", !ch.isOperator(bob));
	expect("bob invite cleared on remove", !ch.isInvited(bob));
	expect("count back to 1", ch.memberCount() == 1);
	expect("bob left set", bob.channels().count("#c") == 0);

	std::printf("units: Channel ok\n");
}

// ------------------------------------------------- IrcException ユニット検査

// - code/target/detail/what() が構築時の値と一致
static void runExcept() {
	IrcException e(Reply::ERR_NEEDMOREPARAMS, "bob", "JOIN :Not enough parameters");
	expect("exc code", e.code() == 461);
	checkEq(e.target(), "bob", "exc target");
	checkEq(e.detail(), "JOIN :Not enough parameters", "exc detail");
	checkEq(std::string(e.what()), "JOIN :Not enough parameters", "exc what()");

	IrcException e2(Reply::ERR_NOTREGISTERED, "", ":You have not registered");
	expect("exc2 code", e2.code() == 451);
	checkEq(e2.target(), "", "exc2 empty target");

	std::printf("units: IrcException ok\n");
}

// - StringUtil / Client / Channel / IrcException をまとめて実行
static void runUnits() {
	runStringUtil();
	runClient();
	runChannel();
	runExcept();
}

// ------------------------------------------------------------------- main

// - モード分岐。"all" は全項目を既定パラメータで実行し、失敗数を終了コードに反映
int main(int argc, char** argv) {
	std::string mode = argc > 1 ? argv[1] : "all";
	long arg = argc > 2 ? std::atol(argv[2]) : -1;

	if (mode == "exhaustive") {
		runExhaustive(arg > 0 ? static_cast<int>(arg) : 12);
	} else if (mode == "garbage") {
		runGarbage(arg > 0 ? static_cast<unsigned long>(arg) : 2000000UL);
	} else if (mode == "roundtrip") {
		runRoundtrip(arg > 0 ? static_cast<unsigned long>(arg) : 1000000UL);
	} else if (mode == "subst") {
		runSubstitution(arg > 0 ? static_cast<unsigned long>(arg) : 500000UL);
	} else if (mode == "reply") {
		runReply();
	} else if (mode == "units") {
		runUnits();
	} else if (mode == "all") {
		runExhaustive(arg > 0 ? static_cast<int>(arg) : 12);
		runGarbage(2000000UL);
		runRoundtrip(1000000UL);
		runSubstitution(500000UL);
		runReply();
		runUnits();
	} else {
		std::fprintf(stderr,
				"usage: %s [all|exhaustive|garbage|roundtrip|subst|reply|units] [N]\n",
				argv[0]);
		return 2;
	}

	std::printf("----\nchecks: %lu, failures: %lu -> %s\n",
			g_checks, g_failures, g_failures == 0 ? "OK" : "NG");
	return g_failures == 0 ? 0 : 1;
}
