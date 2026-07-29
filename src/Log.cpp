#include "Log.hpp"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <iostream>

#include "Client.hpp"

namespace {
	const char* const RESET  = "\033[0m";
	const char* const DIM    = "\033[2m";
	const char* const BOLD   = "\033[1m";
	const char* const RED    = "\033[31m";
	const char* const GREEN  = "\033[32m";
	const char* const YELLOW = "\033[33m";
	const char* const BLUE   = "\033[94m";
	const char* const MAGENT = "\033[35m";
	const char* const CYAN   = "\033[36m";
	const char* const GRAY   = "\033[90m";

	// ft_irc（ANSI Shadow体）．各行44カラム．行ごとに濃淡を変えてグラデーションにする
	const char* const ART[] = {
		"███████╗████████╗        ██╗██████╗  ██████╗",
		"██╔════╝╚══██╔══╝        ██║██╔══██╗██╔════╝",
		"█████╗     ██║           ██║██████╔╝██║",
		"██╔══╝     ██║           ██║██╔══██╗██║",
		"██║        ██║    █████╗ ██║██║  ██║╚██████╗",
		"╚═╝        ╚═╝    ╚════╝ ╚═╝╚═╝  ╚═╝ ╚═════╝"
	};
	const char* const ART_COLOR[] = {
		"\033[38;5;183m", "\033[38;5;177m", "\033[38;5;171m",
		"\033[38;5;135m", "\033[38;5;129m", "\033[38;5;93m"
	};
	const std::size_t ART_ROWS = sizeof(ART) / sizeof(ART[0]);
	const std::size_t PANEL_WIDTH = 46;
	const std::size_t KEY_WIDTH = 10;
	const std::size_t TAG_WIDTH = 5;

	// 出力キュー．stdout へ直接書くと読み手が止まった時にブロックするので，
	// ここへ積んで poll(2) の POLLOUT でだけ吐く．
	const std::size_t LOG_QUEUE_CAP = 256UL * 1024;   // 超過分の行は捨てる
	// POLLOUT 直後に PIPE_BUF(POSIX最小512) 以下を書く限りブロックしない．
	// 端末の低水位マークも通常これ以上あるので1回の書き出しはこの粒度に刻む．
	const std::size_t LOG_CHUNK = 512;

	std::string& queue() {
		static std::string q;
		return q;
	}

	unsigned long& dropped() {
		static unsigned long n = 0;
		return n;
	}

	// 容量内なら再確保しない．溢れたら行ごと捨てて dropped を数える
	void push(const std::string& line) {
		std::string& q = queue();
		if (q.size() + line.size() > LOG_QUEUE_CAP) {
			++dropped();
			return;
		}
		q += line;
	}

	// OOM 経路用．std::string を作らず，容量内に収まる時だけ追記する
	void pushRaw(const char* s) {
		if (s == 0) {
			return;
		}
		std::string& q = queue();
		std::size_t len = std::strlen(s);
		if (q.size() + len > q.capacity()) {
			++dropped();
			return;
		}
		q.append(s, len);
	}

	bool envOn(const char* name) {
		const char* v = std::getenv(name);
		return v != 0 && v[0] != '\0' && !(v[0] == '0' && v[1] == '\0');
	}

	// NO_COLOR（デファクト標準）と TERM=dumb を尊重．判定は初回だけ
	bool detectColor() {
		if (envOn("NO_COLOR")) {
			return false;
		}
		const char* term = std::getenv("TERM");
		return !(term != 0 && std::string(term) == "dumb");
	}

	bool colorOn() {
		static const bool on = detectColor();
		return on;
	}

	std::string paint(const char* code, const std::string& s) {
		if (!colorOn()) {
			return s;
		}
		return std::string(code) + s + RESET;
	}

	// 動的確保をしない時刻整形（OOM経路から呼ぶため char 配列に直接書く）
	void stampInto(char* buf, std::size_t n) {
		std::time_t now = std::time(0);
		std::tm* tmv = std::localtime(&now);
		if (tmv == 0 || std::strftime(buf, n, "%H:%M:%S", tmv) == 0) {
			std::strncpy(buf, "--:--:--", n - 1);
			buf[n - 1] = '\0';
		}
	}

	std::string stamp() {
		char buf[16];
		stampInto(buf, sizeof(buf));
		return buf;
	}

	std::string pad(const std::string& s, std::size_t width) {
		std::string r(s);
		while (r.size() < width) {
			r += ' ';
		}
		return r;
	}

	// ログ本文にはnick/QUIT理由など client 由来の文字列が入る．制御文字を落として
	// 端末エスケープの注入（例 QUIT :<ESC>[2J）を防ぐ．0x80以上は UTF-8 としてそのまま通す
	std::string sanitize(const std::string& s) {
		std::string r;
		r.reserve(s.size());
		for (std::string::size_type i = 0; i < s.size(); ++i) {
			unsigned char c = static_cast<unsigned char>(s[i]);
			r += (c < 0x20 || c == 0x7F) ? '.' : s[i];
		}
		return r;
	}

	void emit(const char* tag, const char* color, const std::string& text) {
		push(paint(DIM, "[" + stamp() + "]") + " "
				+ paint(color, pad(tag, TAG_WIDTH)) + "  " + sanitize(text)
				+ "\n");
	}

	// 動的確保をしない emit。std::string を一切作らず、確保済み容量へ直接追記する。
	// OOM ハンドラから呼ばれるので paint()/pad()/sanitize()/push() は使えない
	void emitNoAlloc(const char* tag, const char* color, const char* a,
			const char* b, const char* c) {
		char ts[16];
		stampInto(ts, sizeof(ts));
		const bool col = colorOn();
		if (col) {
			pushRaw(DIM);
		}
		pushRaw("[");
		pushRaw(ts);
		pushRaw("]");
		if (col) {
			pushRaw(RESET);
			pushRaw(" ");
			pushRaw(color);
		} else {
			pushRaw(" ");
		}
		pushRaw(tag);
		if (col) {
			pushRaw(RESET);
		}
		pushRaw("   ! ");
		pushRaw(a);
		if (b != 0) {
			pushRaw(" ");
			pushRaw(b);
		}
		if (c != 0) {
			pushRaw(": ");
			pushRaw(c);
		}
		pushRaw("\n");
	}
}

void Log::reserve() {
	queue().reserve(LOG_QUEUE_CAP);
}

bool Log::hasPending() {
	return !queue().empty();
}

unsigned long Log::droppedLines() {
	return dropped();
}

// POLLOUT が立った時だけ呼ぶ．1回に LOG_CHUNK までしか書かないのでブロックしない．
std::size_t Log::drainOnce() {
	std::string& q = queue();
	if (q.empty()) {
		return 0;
	}
	const std::size_t n = q.size() < LOG_CHUNK ? q.size() : LOG_CHUNK;
	std::cout.write(q.data(), static_cast<std::streamsize>(n));
	std::cout.flush();
	if (!std::cout.good()) {
		// 書けなかった分は捨てて進む（ログのために本業を止めない）
		std::cout.clear();
	}
	q.erase(0, n);
	return n;
}

void Log::banner(const std::string& serverName, const std::string& version) {
	push("\n");
	for (std::size_t i = 0; i < ART_ROWS; ++i) {
		push(" " + paint(ART_COLOR[i], ART[i]) + "\n");
	}
	push("  " + paint(BOLD, serverName + " " + version)
			+ paint(DIM, "  C++98 / single-process poll(2) event loop") + "\n");
	rule();
}

void Log::field(const std::string& key, const std::string& value) {
	push("  " + paint(GRAY, pad(key, KEY_WIDTH)) + value + "\n");
}

void Log::rule() {
	std::string line;
	for (std::size_t i = 0; i < PANEL_WIDTH; ++i) {
		line += "─";
	}
	push(" " + paint(DIM, line) + "\n");
}

void Log::info(const std::string& text) {
	emit("INFO", CYAN, text);
}

void Log::warn(const std::string& text) {
	emit("WARN", YELLOW, text);
}

void Log::err(const std::string& text) {
	emit("FATAL", RED, text);
}

void Log::conn(const std::string& text) {
	emit("CONN", GREEN, text);
}

void Log::auth(const std::string& text) {
	emit("AUTH", MAGENT, text);
}

void Log::chan(const std::string& text) {
	emit("CHAN", CYAN, text);
}

void Log::memb(const std::string& text) {
	emit("MEMB", BLUE, text);
}

void Log::mode(const std::string& text) {
	emit("MODE", MAGENT, text);
}

void Log::deny(const std::string& text) {
	emit("DENY", YELLOW, text);
}

// relay/trace は1メッセージ毎に出るので既定オフ。stdout はブロッキングなので、
// 既定では悪意ある flood でログ量が無制限に増えないようにしておく
void Log::relay(const std::string& text) {
	if (traceEnabled()) {
		emit("MSG", GRAY, text);
	}
}

void Log::trace(const std::string& text) {
	if (traceEnabled()) {
		emit("RECV", GRAY, text);
	}
}

void Log::oomWarn(const char* what, const char* detail, const char* extra) {
	try {
		emitNoAlloc("WARN", YELLOW, what, detail, extra);
	}
	// メモリ不足の経路。ここで投げ返すと run() を抜けて main が終了してしまうので
	// （要件: OOMでも落ちない・予期せず終了しない）意図的に握り潰す
	// NOLINTNEXTLINE(bugprone-empty-catch)
	catch (...) {
	}
}

bool Log::traceEnabled() {
	static const bool on = envOn("IRC_TRACE");
	return on;
}

std::string Log::who(const Client& client) {
	if (client.hasNick()) {
		return client.nick();
	}
	return "*@" + client.host();
}
