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

static std::string upperAscii(const std::string& s) {
	std::string r(s);
	for (std::string::size_type i = 0; i < r.size(); ++i) {
		r[i] = static_cast<char>(
				std::toupper(static_cast<unsigned char>(r[i])));
	}
	return r;
}

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

static void fail(const std::string& what, const std::string& input) {
	++g_failures;
	std::fprintf(stderr, "FAIL %s input=%s\n", what.c_str(), quoted(input).c_str());
}

static void expect(const char* what, bool cond) {
	++g_checks;
	if (!cond) {
		++g_failures;
		std::fprintf(stderr, "FAIL %s\n", what);
	}
}

static void checkEq(const std::string& actual, const std::string& expected,
		const std::string& what) {
	++g_checks;
	if (actual != expected) {
		++g_failures;
		std::fprintf(stderr, "FAIL %s\n  got  %s\n  want %s\n",
				what.c_str(), quoted(actual).c_str(), quoted(expected).c_str());
	}
}

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

	for (std::size_t i = 0; i + 1 < m.size(); ++i) {
		const std::string& p = m.param(i);
		if (p.empty()) { fail("INV middle param empty", line); break; }
		if (p[0] == ':') { fail("INV middle param starts ':'", line); break; }
		if (p.find(' ') != std::string::npos) { fail("INV middle param has space", line); break; }
	}
	if (m.empty() && (!m.prefix().empty() || m.size() != 0)) {
		fail("INV invalid msg retains fields", line);
	}
}

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

static void runGarbage(unsigned long iters) {
	Rng rng(0x5EED0001UL);
	for (unsigned long it = 0; it < iters; ++it) {
		std::string s;
		unsigned long len = rng.below(40);
		for (unsigned long i = 0; i < len; ++i) {
			unsigned long r = rng.below(100);
			char c;
			if (r < 30) c = ' ';
			else if (r < 55) c = ':';
			else c = static_cast<char>(static_cast<unsigned char>(rng.below(256)));
			s += c;
		}
		checkAgainstSpec(s);
		checkInvariants(s);
	}
	std::printf("T1'/T4: %lu random byte strings, impl==spec + invariants\n", iters);
}

static std::string randToken(Rng& rng, unsigned long maxLen, bool noLeadingColon) {
	unsigned long len = 1 + rng.below(maxLen);
	std::string t;
	for (unsigned long i = 0; i < len; ++i) {
		char c;
		do {
			c = static_cast<char>(33 + rng.below(94));
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
				line += trailing;
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

static void buildSigma(Rng& rng, char sigma[256]) {
	for (int i = 0; i < 256; ++i) sigma[i] = static_cast<char>(i);
	int rotL = static_cast<int>(rng.below(26));
	int rotD = static_cast<int>(rng.below(10));
	for (int i = 0; i < 26; ++i) {
		sigma['a' + i] = static_cast<char>('a' + (i + rotL) % 26);
		sigma['A' + i] = static_cast<char>('A' + (i + rotL) % 26);
	}
	for (int i = 0; i < 10; ++i) {
		sigma['0' + i] = static_cast<char>('0' + (i + rotD) % 10);
	}
}

static std::string applySigma(const char sigma[256], const std::string& s) {
	std::string r(s);
	for (std::string::size_type i = 0; i < r.size(); ++i) {
		r[i] = sigma[static_cast<unsigned char>(r[i])];
	}
	return r;
}

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

static void runStringUtil() {
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

	checkEq(StringUtil::capLine("short"), "short", "capLine <=510 passthrough");
	expect("capLine >510 truncated to 510",
			StringUtil::capLine(std::string(600, 'x')).size() == 510);

	std::printf("units: StringUtil ok\n");
}

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

static void runClient() {
	{
		const char* e1[] = { "ABC" };
		feedAndExpectLines("ABC\r\n", e1, 1);
		const char* e3[] = { "A", "B" };
		feedAndExpectLines("A\r\nB\r\n", e3, 2);
		const char* e4[] = { "" };
		feedAndExpectLines("\r\n", e4, 1);
	}
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
	{
		Client c(-1, "h");
		std::string line;
		c.appendInput("X\rY\r\n", 5);
		expect("embedded CR line completes", c.extractLine(line));
		expect("embedded CR rejected", c.inputProtocolError());
		expect("embedded CR clears output", line.empty());
	}
	{
		Client c(-1, "h");
		std::string line;
		c.appendInput("AB", 2);
		expect("partial: no line yet", !c.extractLine(line));
		c.appendInput("C\r\n", 3);
		expect("partial: line completes", c.extractLine(line));
		checkEq(line, "ABC", "partial reassembled");
	}
	{
		Client c(-1, "h");
		std::string line;
		const char raw[] = "A\0BC\r\n";
		c.appendInput(raw, sizeof(raw) - 1);
		expect("nul line completes", c.extractLine(line));
		expect("nul rejected", c.inputProtocolError());
		expect("nul clears output", line.empty());
	}
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
	{
		Client d(4, "h");
		expect("fd getter", d.fd() == 4);
		checkEq(d.host(), "h", "host getter");
		expect("no pending out", !d.hasPendingOutput());
		expect("not read-closed", !d.isReadClosed());
		expect("connectedAt set at ctor", d.connectedAt() > 0);
		expect("closingSince zero before close", d.closingSince() == 0);
		d.appendOutput("xx");
		expect("has pending out", d.hasPendingOutput());
		checkEq(d.outBuffer(), "xx", "outBuffer");
		d.markReadClosed();
		expect("read closed", d.isReadClosed());
		expect("closingSince set after close", d.closingSince() > 0);
		std::time_t cs = d.closingSince();
		d.markReadClosed();
		expect("still read closed", d.isReadClosed());
		expect("closingSince unchanged on 2nd close", d.closingSince() == cs);

		Client e(5, "h");
		expect("no input overflow when empty", !e.inputOverflow());
		std::string big(600, 'A');
		e.appendInput(big.data(), big.size());
		expect("input overflow", e.inputOverflow());
		e.appendInput("\r\n", 2);
		expect("no overflow with newline", !e.inputOverflow());

		Client f(6, "h");
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

static void runChannel() {
	Client creator(1, "h");
	Client bob(2, "h");
	Client stranger(3, "h");

	Channel ch("#c", creator);
	checkEq(ch.name(), "#c", "channel name");
	expect("creator is member", ch.hasMember(creator));
	expect("creator is operator", ch.isOperator(creator));
	expect("count 1", ch.memberCount() == 1);
	expect("not empty", !ch.isEmpty());
	expect("creator joined set", creator.channels().count("#c") == 1);
	checkEq(ch.modeString(creator), "+", "modeString empty");

	ch.addMember(bob);
	expect("bob member", ch.hasMember(bob));
	expect("bob not op", !ch.isOperator(bob));
	expect("count 2", ch.memberCount() == 2);
	expect("bob joined set", bob.channels().count("#c") == 1);
	expect("members() reflects count", ch.members().size() == ch.memberCount());

	ch.addOperator(bob);
	expect("bob op", ch.isOperator(bob));
	ch.removeOperator(bob);
	expect("bob deop", !ch.isOperator(bob));

	ch.invite(bob);
	expect("bob invited", ch.isInvited(bob));
	ch.clearInvite(bob);
	expect("bob invite cleared", !ch.isInvited(bob));

	expect("no topic init", !ch.hasTopic());
	ch.setTopic("hello", "creator");
	expect("has topic", ch.hasTopic());
	checkEq(ch.topic(), "hello", "topic value");
	checkEq(ch.topicSetter(), "creator", "topic setter");
	ch.setTopic("", "creator");
	expect("empty topic clears", !ch.hasTopic());

	ch.setInviteOnly(true);
	ch.setTopicLocked(true);
	expect("inviteOnly getter", ch.inviteOnly());
	expect("topicLocked getter", ch.topicLocked());
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

	{
		std::string baseC = creator.outBuffer();
		std::string baseB = bob.outBuffer();
		ch.broadcast("HELLO");
		expect("broadcast reaches creator", creator.outBuffer() == baseC + "HELLO\r\n");
		expect("broadcast reaches bob", bob.outBuffer() == baseB + "HELLO\r\n");
		std::string midC = creator.outBuffer();
		std::string midB = bob.outBuffer();
		ch.broadcast("SOLO", &creator);
		expect("broadcast except skips creator", creator.outBuffer() == midC);
		expect("broadcast except reaches bob", bob.outBuffer() == midB + "SOLO\r\n");
	}

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

static void runUnits() {
	runStringUtil();
	runClient();
	runChannel();
	runExcept();
}

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
