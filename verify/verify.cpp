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
//     - RFC整形式メッセージのラウンドトリップ
//     - command文法、区切り、最大15 paramsの境界ベクタ
//     - 文字クラス抽象化補題（置換σとの可換性）← 全長への一般化の根拠
//     - 範囲外 param(i) が常に空（メモリ安全）
//   [Reply 整形]  ../src/Reply.cpp
//     - numeric() をコード0..999全数×target/rest代表で独立実装と一致
//     - RFC形式ゴールデンベクタ / from() / 数値定数が定義域[1,999]
//   [StringUtil]  ../src/StringUtil.cpp
//     - ircCaseFold/trim/split/toString の代表・境界ケース
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
//   長さ<=12 の全列挙は、その有限領域での実装≡仕様の証明に限られる。
//   15 paramsなど長さ依存の境界は専用ベクタ、長い入力はproperty/fuzzで別途検証する。
//
// 使い方:
//   ./verify all [maxLen]   … 全項目（T1は長さ<=maxLen 全数、既定12）
//   ./verify exhaustive N   … Message実装≡仕様（縮約アルファベット長さ<=N 全数）
//   ./verify garbage ITERS  … ランダム任意バイト列で差分＋不変条件
//   ./verify roundtrip ITERS… parse(serialize(m)) == normalize(m)
//   ./verify subst ITERS    … 文字クラス抽象化補題（σとの可換性）
//   ./verify parser         … MessageのRFC境界ベクタ
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
	if (!m.empty()) {
		bool letters = true;
		bool digits = cmd.size() == 3;
		for (std::string::size_type i = 0; i < cmd.size(); ++i) {
			if (cmd[i] < 'A' || cmd[i] > 'Z') letters = false;
			if (cmd[i] < '0' || cmd[i] > '9') digits = false;
		}
		if (!letters && !digits) fail("INV command grammar", line);
	}
	const std::string& pre = m.prefix();
	if (pre.find(' ') != std::string::npos) fail("INV prefix has space", line);
	if (m.size() > 15) fail("INV too many params", line);

	for (std::size_t i = 0; i + 1 < m.size(); ++i) {  // 最後以外のパラメータ
		const std::string& p = m.param(i);
		if (p.empty()) { fail("INV middle param empty", line); break; }
		if (p[0] == ':') { fail("INV middle param starts ':'", line); break; }
		if (p.find(' ') != std::string::npos) { fail("INV middle param has space", line); break; }
	}
	if (m.empty() && (!m.prefix().empty() || m.size() != 0)) {
		fail("INV invalid msg retains fields", line);
	}
}

// - T1本体: 区切り、英字command、数値commandを含む縮約アルファベットの全列挙
static void runExhaustive(int maxLen) {
	static const char ALPHA[] = { ' ', ':', 'a', '1' };
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
	std::printf("T1: exhaustive impl==spec, %lu strings, alphabet {' ',':','a','1'}, len<=%d\n",
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

static std::string randCommand(Rng& rng, unsigned long maxLen) {
	unsigned long len = 1 + rng.below(maxLen);
	std::string command;
	for (unsigned long i = 0; i < len; ++i) {
		unsigned long letter = rng.below(52);
		command += static_cast<char>(
				letter < 26 ? 'A' + letter : 'a' + letter - 26);
	}
	return command;
}

// - T3本体: 整形式メッセージを直列化→parseで完全復元されることを検査
static void runRoundtrip(unsigned long iters) {
	Rng rng(0x5EED0002UL);
	for (unsigned long it = 0; it < iters; ++it) {
		bool hasPrefix = rng.below(2) == 0;
		std::string prefix = hasPrefix ? randToken(rng, 10, false) : "";
		std::string cmd = randCommand(rng, 8);

		std::vector<std::string> params;
		unsigned long nMiddle = rng.below(15);
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
		if (hasPrefix) { line += ':'; line += prefix; line += ' '; }
		line += cmd;
		for (std::size_t i = 0; i < params.size(); ++i) {
			line += ' ';
			line += params[i];
		}
		if (hasTrailing) {
			line += ' ';
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

// - RFC 2812 §2.3.1で長さ依存になるcommand/params境界を明示的に検証する
static void runParserBoundaries() {
	static const char* valid[] = {
		"PING",
		"001",
		":server PING token",
		"PRIVMSG #room :hello world",
		"CMD :"
	};
	for (std::size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i) {
		checkAgainstSpec(valid[i]);
		expect("valid RFC message accepted", !Message::parse(valid[i]).empty());
	}

	static const char* invalid[] = {
		" PING",
		"PING  token",
		": PING",
		":server  PING",
		":server",
		"A1",
		"12",
		"1234",
		"_",
		"{",
		"/12",
		"PING ",
		"PING\r",
		"PING\n",
		"PI\0NG"
	};
	for (std::size_t i = 0; i + 1 < sizeof(invalid) / sizeof(invalid[0]); ++i) {
		checkAgainstSpec(invalid[i]);
		expect("invalid RFC message rejected", Message::parse(invalid[i]).empty());
	}
	std::string withNul(invalid[sizeof(invalid) / sizeof(invalid[0]) - 1], 5);
	checkAgainstSpec(withNul);
	expect("NUL message rejected", Message::parse(withNul).empty());

	std::string fourteen = "CMD";
	for (int i = 0; i < 14; ++i) {
		fourteen += " p";
	}
	checkAgainstSpec(fourteen);
	expect("fourteen middle params accepted",
			Message::parse(fourteen).size() == 14);

	std::string fifteen = fourteen + " final trailing value";
	checkAgainstSpec(fifteen);
	Message parsed = Message::parse(fifteen);
	expect("fifteen params accepted", parsed.size() == 15);
	checkEq(parsed.param(14), "final trailing value",
			"fifteenth param is trailing");

	std::string fifteenColon = fourteen + " :final trailing value";
	checkAgainstSpec(fifteenColon);
	expect("colon-prefixed fifteenth param accepted",
			Message::parse(fifteenColon).size() == 15);

	std::string badSeparator = fourteen + "  invalid";
	checkAgainstSpec(badSeparator);
	expect("double separator at param boundary rejected",
			Message::parse(badSeparator).empty());

	std::string missingFifteenth = fourteen + " ";
	checkAgainstSpec(missingFifteenth);
	expect("missing fifteenth param rejected",
			Message::parse(missingFifteenth).empty());

	std::printf("parser: RFC command/separator/15-param boundaries ok\n");
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

// - ircCaseFold/split/toString の代表・境界（split は空要素/区切り無しも）
static void runStringUtil() {
	// casemapping (RFC 2812 §2.2 / RFC 2811 §2.2): A-Z 小文字化 + {}|^ -> []\~
	checkEq(StringUtil::ircCaseFold("aB{}|^C:1"), "ab[]\\~c:1", "ircCaseFold");

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

	// capLine（RFC2812 §2.3）: 510以下はそのまま、超過は510に切り詰め
	checkEq(StringUtil::capLine("short"), "short", "capLine <=510 passthrough");
	expect("capLine >510 truncated to 510",
			StringUtil::capLine(std::string(600, 'x')).size() == 510);

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

// - CRLF/空行/複数行/不正終端/部分受信 と 登録状態機械・prefix・buffer/overflow
static void runClient() {
	// 行再構築の各パターン
	{
		const char* e1[] = { "ABC" };
		feedAndExpectLines("ABC\r\n", e1, 1);
		const char* e3[] = { "A", "B" };
		feedAndExpectLines("A\r\nB\r\n", e3, 2);       // 複数行
		const char* e4[] = { "" };
		feedAndExpectLines("\r\n", e4, 1);              // 空行
	}
	// RFC 2812の終端は必ずCRLF。bare LFは内容にかかわらず拒否する。
	{
		Client c(-1, "h");
		std::string line;
		c.appendInput("ABC\n", 4);
		expect("bare LF line completes", c.extractLine(line));
		expect("bare LF rejected", c.inputProtocolError());
		expect("bare LF clears output", line.empty());
	}
	{
		Client c(-1, "h");
		std::string line;
		c.appendInput("\n", 1);
		expect("empty bare LF completes", c.extractLine(line));
		expect("empty bare LF rejected", c.inputProtocolError());
	}
	// 埋め込みCRは内容を書き換えず、プロトコルエラーとして扱う。
	{
		Client c(-1, "h");
		std::string line;
		c.appendInput("X\rY\r\n", 5);
		expect("embedded CR line completes", c.extractLine(line));
		expect("embedded CR rejected", c.inputProtocolError());
		expect("embedded CR clears output", line.empty());
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
	// NULはRFC2812 §2.3.1違反として拒否する。
	{
		Client c(-1, "h");
		std::string line;
		const char raw[] = "A\0BC\r\n";        // A NUL B C CR LF
		c.appendInput(raw, sizeof(raw) - 1);   // 6 bytes（末尾の実NUL終端は除く）
		expect("nul line completes", c.extractLine(line));
		expect("nul rejected", c.inputProtocolError());
		expect("nul clears output", line.empty());
	}
	// RFC上限はCRLF込み512 octets。510-byte本体は可、511-byte本体は拒否。
	{
		Client c(-1, "h");
		std::string line;
		std::string boundary(510, 'A');
		boundary += "\r\n";
		c.appendInput(boundary.data(), boundary.size());
		expect("512-octet line completes", c.extractLine(line));
		expect("512-octet line accepted", !c.inputProtocolError());
		expect("512-octet payload preserved", line.size() == 510);
	}
	{
		Client c(-1, "h");
		std::string line;
		std::string overlong(511, 'A');
		overlong += "\r\n";
		c.appendInput(overlong.data(), overlong.size());
		expect("513-octet line completes", c.extractLine(line));
		expect("513-octet line rejected", c.inputProtocolError());
		expect("513-octet line clears output", line.empty());
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
		expect("fd getter", d.fd() == 4);           // getter 網羅
		checkEq(d.host(), "h", "host getter");      // getter 網羅
		expect("no pending out", !d.hasPendingOutput());
		expect("not read-closed", !d.isReadClosed());
		expect("connectedAt set at ctor", d.connectedAt() > 0);  // connectedAt getter 網羅
		expect("closingSince zero before close", d.closingSince() == 0);
		d.appendOutput("xx");
		expect("has pending out", d.hasPendingOutput());
		checkEq(d.outBuffer(), "xx", "outBuffer");
		d.markReadClosed();                         // !_readClosed True 分岐
		expect("read closed", d.isReadClosed());
		expect("closingSince set after close", d.closingSince() > 0);  // closingSince getter 網羅
		std::time_t cs = d.closingSince();
		d.markReadClosed();                         // 2回目: !_readClosed False 分岐
		expect("still read closed", d.isReadClosed());
		expect("closingSince unchanged on 2nd close", d.closingSince() == cs);

		Client e(5, "h");
		expect("no input overflow when empty", !e.inputOverflow());  // size<=512 の False 分岐
		std::string big(600, 'A');              // 512超・改行なし
		e.appendInput(big.data(), big.size());
		expect("input overflow", e.inputOverflow());
		e.appendInput("\r\n", 2);               // 改行が来ればextractLine側で長さ判定
		expect("no overflow with newline", !e.inputOverflow());

		Client f(6, "h");                       // outputOverflow 網羅（<=1MiB と >1MiB）
		expect("no out overflow when small", !f.outputOverflow());
		f.appendOutput(std::string(1024 * 1024 + 1, 'x'));
		expect("out overflow when huge", f.outputOverflow());
		expect("overflowing write is not queued", f.outBuffer().empty());
		f.appendOutput("ignored");
		expect("writes after overflow stay ignored", f.outBuffer().empty());

		Client g(7, "h");
		g.appendOutput(std::string(1024 * 1024, 'x'));
		expect("queue exactly at limit accepted",
				!g.outputOverflow()
				&& g.outBuffer().size() == 1024UL * 1024);
		g.appendOutput("x");
		expect("one byte past full queue rejected", g.outputOverflow());
	}
	std::printf("units: Client ok\n");
}

// ------------------------------------------------- Channel ユニット検査

// - 生成時 creator=member+operator、membership/operator/invite/topic、mode整形
static void runChannel() {
	Client creator(1, "h");
	Client bob(2, "h");
	Client stranger(3, "h");   // チャンネルに参加しない非メンバ視点(324 の鍵/上限マスク検証用)

	Channel ch("#c", creator);
	checkEq(ch.name(), "#c", "channel name");
	expect("creator is member", ch.hasMember(creator));
	expect("creator is operator", ch.isOperator(creator));
	expect("count 1", ch.memberCount() == 1);
	expect("not empty", !ch.isEmpty());
	expect("creator joined set", creator.channels().count("#c") == 1);
	checkEq(ch.modeString(creator), "+", "modeString empty");

	// membership
	ch.addMember(bob);
	expect("bob member", ch.hasMember(bob));
	expect("bob not op", !ch.isOperator(bob));
	expect("count 2", ch.memberCount() == 2);
	expect("bob joined set", bob.channels().count("#c") == 1);
	expect("members() reflects count", ch.members().size() == ch.memberCount());  // getter 網羅

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
	expect("inviteOnly getter", ch.inviteOnly());     // getter 網羅
	expect("topicLocked getter", ch.topicLocked());   // getter 網羅
	checkEq(ch.modeString(creator), "+it", "modeString +it");
	ch.setKey("secret");
	expect("has key", ch.hasKey());
	checkEq(ch.key(), "secret", "key value");
	checkEq(ch.modeString(creator), "+itk secret", "modeString +itk");
	checkEq(ch.modeString(stranger), "+itk *", "modeString +itk key masked (non-member)");
	ch.setLimit(5);
	expect("has limit", ch.hasLimit());
	expect("limit 5", ch.limit() == 5);
	checkEq(ch.modeString(creator), "+itkl secret 5", "modeString +itkl");
	checkEq(ch.modeString(stranger), "+itkl * *", "modeString +itkl key/limit masked (non-member)");
	ch.clearKey();
	expect("key cleared", !ch.hasKey());
	checkEq(ch.modeString(creator), "+itl 5", "modeString +itl");
	checkEq(ch.modeString(stranger), "+itl *", "modeString +itl limit masked (non-member)");
	ch.clearLimit();
	expect("limit cleared", !ch.hasLimit());
	checkEq(ch.modeString(creator), "+it", "modeString back to +it");

	// broadcast: except=0 は全員、except=&creator は creator を除外（Channel_broadcast 網羅）
	{
		std::string baseC = creator.outBuffer();
		std::string baseB = bob.outBuffer();
		ch.broadcast("HELLO");                    // except 既定=0 → 全員へ
		expect("broadcast reaches creator", creator.outBuffer() == baseC + "HELLO\r\n");
		expect("broadcast reaches bob", bob.outBuffer() == baseB + "HELLO\r\n");
		std::string midC = creator.outBuffer();
		std::string midB = bob.outBuffer();
		ch.broadcast("SOLO", &creator);           // creator を除外
		expect("broadcast except skips creator", creator.outBuffer() == midC);
		expect("broadcast except reaches bob", bob.outBuffer() == midB + "SOLO\r\n");
	}

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
	} else if (mode == "parser") {
		runParserBoundaries();
	} else if (mode == "reply") {
		runReply();
	} else if (mode == "units") {
		runUnits();
	} else if (mode == "all") {
		runExhaustive(arg > 0 ? static_cast<int>(arg) : 12);
		runGarbage(2000000UL);
		runRoundtrip(1000000UL);
		runSubstitution(500000UL);
		runParserBoundaries();
		runReply();
		runUnits();
	} else {
		std::fprintf(stderr,
				"usage: %s [all|exhaustive|garbage|roundtrip|subst|parser|reply|units] [N]\n",
				argv[0]);
		return 2;
	}

	std::printf("----\nchecks: %lu, failures: %lu -> %s\n",
			g_checks, g_failures, g_failures == 0 ? "OK" : "NG");
	return g_failures == 0 ? 0 : 1;
}
