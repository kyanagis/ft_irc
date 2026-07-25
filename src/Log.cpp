#include "Log.hpp"

#include <cstddef>
#include <cstdlib>
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
		"\033[38;5;87m", "\033[38;5;51m", "\033[38;5;45m",
		"\033[38;5;39m", "\033[38;5;33m", "\033[38;5;27m"
	};
	const std::size_t ART_ROWS = sizeof(ART) / sizeof(ART[0]);
	const std::size_t PANEL_WIDTH = 46;
	const std::size_t KEY_WIDTH = 10;
	const std::size_t TAG_WIDTH = 5;

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

	std::string stamp() {
		std::time_t now = std::time(0);
		std::tm* tmv = std::localtime(&now);
		char buf[16];
		if (tmv != 0 && std::strftime(buf, sizeof(buf), "%H:%M:%S", tmv) > 0) {
			return buf;
		}
		return "--:--:--";
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
		std::cout << paint(DIM, "[" + stamp() + "]") << " "
				<< paint(color, pad(tag, TAG_WIDTH)) << "  " << sanitize(text)
				<< std::endl;
	}
}

void Log::banner(const std::string& serverName, const std::string& version) {
	std::cout << std::endl;
	for (std::size_t i = 0; i < ART_ROWS; ++i) {
		std::cout << " " << paint(ART_COLOR[i], ART[i]) << std::endl;
	}
	std::cout << "  " << paint(BOLD, serverName + " " + version)
			<< paint(DIM, "  C++98 / single-process poll(2) event loop")
			<< std::endl;
	rule();
}

void Log::field(const std::string& key, const std::string& value) {
	std::cout << "  " << paint(GRAY, pad(key, KEY_WIDTH)) << value << std::endl;
}

void Log::rule() {
	std::string line;
	for (std::size_t i = 0; i < PANEL_WIDTH; ++i) {
		line += "─";
	}
	std::cout << " " << paint(DIM, line) << std::endl;
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

void Log::relay(const std::string& text) {
	emit("MSG", GRAY, text);
}

void Log::deny(const std::string& text) {
	emit("DENY", YELLOW, text);
}

void Log::trace(const std::string& text) {
	if (traceEnabled()) {
		emit("RECV", GRAY, text);
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
