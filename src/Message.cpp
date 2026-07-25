#include "Message.hpp"

namespace {
	const std::string EMPTY_PARAM = "";

	std::string toUpperAscii(const std::string& s) {
		std::string result(s);
		for (std::string::size_type i = 0; i < result.size(); ++i) {
			// isCommand()通過後なので、'Z'より大きい文字は小文字だけ。
			if (result[i] > 'Z') {
				result[i] = static_cast<char>(result[i] - 'a' + 'A');
			}
		}
		return result;
	}

	bool isLetter(char c) {
		return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
	}

	bool isDigit(char c) {
		return c >= '0' && c <= '9';
	}

	bool isCommand(const std::string& command) {
		bool allLetters = !command.empty();
		for (std::string::size_type i = 0; i < command.size(); ++i) {
			if (!isLetter(command[i])) {
				allLetters = false;
			}
		}
		if (allLetters) {
			return true;
		}
		return command.size() == 3
				&& isDigit(command[0])
				&& isDigit(command[1])
				&& isDigit(command[2]);
	}

	bool containsForbiddenByte(const std::string& line) {
		for (std::string::size_type i = 0; i < line.size(); ++i) {
			if (line[i] == '\0' || line[i] == '\r' || line[i] == '\n') {
				return true;
			}
		}
		return false;
	}
}

Message::Message()
		: _prefix(),
			_command(),
			_params() {
}

Message Message::parse(const std::string& line) {
	Message msg;
	if (line.empty() || line[0] == ' ' || containsForbiddenByte(line)) {
		return msg;
	}

	std::string::size_type pos = 0;

	if (line[pos] == ':') {
		std::string::size_type end = line.find(' ', pos + 1);
		if (end == std::string::npos || end == pos + 1
				|| end + 1 >= line.size() || line[end + 1] == ' ') {
			return Message();
		}
		msg._prefix = line.substr(pos + 1, end - pos - 1);
		pos = end + 1;
	}

	std::string::size_type end = line.find(' ', pos);
	if (end == std::string::npos) {
		end = line.size();
	}
	std::string command = line.substr(pos, end - pos);
	if (!isCommand(command)) {
		return Message();
	}
	msg._command = toUpperAscii(command);
	if (end == line.size()) {
		return msg;
	}

	pos = end + 1;
	if (pos >= line.size() || line[pos] == ' ') {
		return Message();
	}

	while (true) {
		if (line[pos] == ':') {
			msg._params.push_back(line.substr(pos + 1));
			return msg;
		}

		end = line.find(' ', pos);
		if (end == std::string::npos) {
			msg._params.push_back(line.substr(pos));
			return msg;
		}
		msg._params.push_back(line.substr(pos, end - pos));

		// RFC 2812 §2.3.1: 最大14個のmiddleと、任意で1個のtrailing。
		// 15番目は':'を省略でき、その場合は残り全体がtrailingになる。
		if (msg._params.size() == 14) {
			pos = end + 1;
			if (pos >= line.size() || line[pos] == ' ') {
				return Message();
			}
			if (line[pos] == ':') {
				++pos;
			}
			msg._params.push_back(line.substr(pos));
			return msg;
		}

		pos = end + 1;
		if (pos >= line.size() || line[pos] == ' ') {
			return Message();
		}
	}
}

bool Message::empty() const {
	return _command.empty();
}

const std::string& Message::prefix() const {
	return _prefix;
}

const std::string& Message::command() const {
	return _command;
}

const std::vector<std::string>& Message::params() const {
	return _params;
}

const std::string& Message::param(std::size_t index) const {
	if (index >= _params.size()) {
		return EMPTY_PARAM;
	}
	return _params[index];
}

std::size_t Message::size() const {
	return _params.size();
}
