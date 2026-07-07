#include "StringUtil.hpp"

#include <cctype>
#include <sstream>

namespace {
	// ロケール非依存でASCII空白のみを空白扱いにする（C++98・isspace()のUB回避）
	bool isAsciiSpace(char c) {
		return c == ' ' || c == '\t' || c == '\r'
				|| c == '\n' || c == '\f' || c == '\v';
	}
}

std::string StringUtil::toUpper(const std::string& s) {
	std::string r(s);
	for (std::string::size_type i = 0; i < r.size(); ++i) {
		r[i] = static_cast<char>(
				std::toupper(static_cast<unsigned char>(r[i])));
	}
	return r;
}

std::string StringUtil::toLower(const std::string& s) {
	std::string r(s);
	for (std::string::size_type i = 0; i < r.size(); ++i) {
		r[i] = static_cast<char>(
				std::tolower(static_cast<unsigned char>(r[i])));
	}
	return r;
}

std::string StringUtil::trim(const std::string& s) {
	std::string::size_type begin = 0;
	std::string::size_type end = s.size();

	while (begin < end && isAsciiSpace(s[begin])) {
		++begin;
	}
	while (end > begin && isAsciiSpace(s[end - 1])) {
		--end;
	}
	return s.substr(begin, end - begin);
}

std::vector<std::string> StringUtil::split(const std::string& s, char delim) {
	std::vector<std::string> tokens;
	std::string::size_type start = 0;

	while (true) {
		std::string::size_type pos = s.find(delim, start);
		if (pos == std::string::npos) {
			tokens.push_back(s.substr(start));
			break;
		}
		tokens.push_back(s.substr(start, pos - start));
		start = pos + 1;
	}
	return tokens;
}

std::string StringUtil::toString(long value) {
	std::ostringstream oss;
	oss << value;
	return oss.str();
}
