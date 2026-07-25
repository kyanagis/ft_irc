#ifndef SPEC_PARSER_HPP
#define SPEC_PARSER_HPP

// RFC 2812 §2.3.1 の message ABNFを、1文字ずつ走査する独立仕様として
// 実装する。src/Message.cppとは制御構造を分け、差分検査に使用する。

#include <string>
#include <vector>

struct SpecMessage {
	std::string              prefix;
	std::string              command;
	std::vector<std::string> params;
	bool                     isEmpty;
};

inline SpecMessage emptySpecMessage() {
	SpecMessage m;
	m.isEmpty = true;
	return m;
}

inline bool specLetter(char c) {
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

inline bool specDigit(char c) {
	return c >= '0' && c <= '9';
}

inline bool specCommandValid(const std::string& command) {
	if (command.empty()) {
		return false;
	}
	bool letters = true;
	for (std::string::size_type i = 0; i < command.size(); ++i) {
		if (!specLetter(command[i])) {
			letters = false;
		}
	}
	if (letters) {
		return true;
	}
	return command.size() == 3
			&& specDigit(command[0])
			&& specDigit(command[1])
			&& specDigit(command[2]);
}

inline SpecMessage specParse(const std::string& line) {
	SpecMessage m = emptySpecMessage();
	if (line.empty() || line[0] == ' ') {
		return m;
	}
	for (std::string::size_type i = 0; i < line.size(); ++i) {
		if (line[i] == '\0' || line[i] == '\r' || line[i] == '\n') {
			return emptySpecMessage();
		}
	}

	std::string::size_type pos = 0;
	if (line[pos] == ':') {
		++pos;
		while (pos < line.size() && line[pos] != ' ') {
			m.prefix += line[pos++];
		}
		if (m.prefix.empty() || pos >= line.size()) {
			return emptySpecMessage();
		}
		++pos;
		if (pos >= line.size() || line[pos] == ' ') {
			return emptySpecMessage();
		}
	}

	std::string command;
	while (pos < line.size() && line[pos] != ' ') {
		command += line[pos++];
	}
	if (!specCommandValid(command)) {
		return emptySpecMessage();
	}
	for (std::string::size_type i = 0; i < command.size(); ++i) {
		if (command[i] >= 'a' && command[i] <= 'z') {
			command[i] = static_cast<char>(command[i] - 'a' + 'A');
		}
	}
	m.command = command;
	m.isEmpty = false;
	if (pos == line.size()) {
		return m;
	}

	++pos;
	if (pos >= line.size() || line[pos] == ' ') {
		return emptySpecMessage();
	}

	while (true) {
		if (line[pos] == ':') {
			m.params.push_back(line.substr(pos + 1));
			return m;
		}

		std::string middle;
		while (pos < line.size() && line[pos] != ' ') {
			middle += line[pos++];
		}
		m.params.push_back(middle);
		if (pos == line.size()) {
			return m;
		}

		if (m.params.size() == 14) {
			++pos;
			if (pos >= line.size() || line[pos] == ' ') {
				return emptySpecMessage();
			}
			if (line[pos] == ':') {
				++pos;
			}
			m.params.push_back(line.substr(pos));
			return m;
		}

		++pos;
		if (pos >= line.size() || line[pos] == ' ') {
			return emptySpecMessage();
		}
	}
}

#endif
