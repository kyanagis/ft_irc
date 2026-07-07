#include "Message.hpp"

#include <cctype>

namespace {
	const std::string EMPTY_PARAM = "";

	std::string toUpperAscii(const std::string& s) {
		std::string result(s);
		for (std::string::size_type i = 0; i < result.size(); ++i) {
			result[i] = static_cast<char>(
					std::toupper(static_cast<unsigned char>(result[i])));
		}
		return result;
	}

	std::string::size_type skipSpaces(const std::string& line,
			std::string::size_type pos) {
		while (pos < line.size() && line[pos] == ' ') {
			++pos;
		}
		return pos;
	}
}

Message::Message()
		: _prefix(),
			_command(),
			_params() {
}

Message Message::parse(const std::string& line) {
	Message msg;
	std::string::size_type pos = skipSpaces(line, 0);

	if (pos < line.size() && line[pos] == ':') {
		std::string::size_type end = line.find(' ', pos);
		if (end == std::string::npos) {
			// prefixのみでコマンドが無い行は空メッセージ扱い
			msg._prefix = line.substr(pos + 1);
			return msg;
		}
		msg._prefix = line.substr(pos + 1, end - pos - 1);
		pos = skipSpaces(line, end);
	}

	std::string::size_type end = line.find(' ', pos);
	if (end == std::string::npos) {
		end = line.size();
	}
	msg._command = toUpperAscii(line.substr(pos, end - pos));
	pos = end;

	while (pos < line.size()) {
		pos = skipSpaces(line, pos);
		if (pos >= line.size()) {
			break;
		}
		if (line[pos] == ':') {
			// trailing: 以降はスペースを含めて丸ごと1パラメータ（空も許容）
			msg._params.push_back(line.substr(pos + 1));
			break;
		}
		end = line.find(' ', pos);
		if (end == std::string::npos) {
			end = line.size();
		}
		msg._params.push_back(line.substr(pos, end - pos));
		pos = end;
	}

	return msg;
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
