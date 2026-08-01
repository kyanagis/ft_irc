#include "StringUtil.hpp"

#include <sstream>

// RFC 2812 §2.2 / RFC 2811 §2.2 の casemapping。ASCII英字に加え、{}|^ は
// []\~ とそれぞれ同一視する。nick/channel の同一性判定に使う（チャンネル鍵の
// 比較には使用しない）。宛先解決と重複判定はいずれも本関数を通すこと。
std::string StringUtil::ircCaseFold(const std::string& s) {
	std::string r(s);
	for (std::string::size_type i = 0; i < r.size(); ++i) {
		if (r[i] >= 'A' && r[i] <= 'Z') {
			r[i] = static_cast<char>(r[i] - 'A' + 'a');
		} else if (r[i] == '{') {
			r[i] = '[';
		} else if (r[i] == '}') {
			r[i] = ']';
		} else if (r[i] == '|') {
			r[i] = '\\';
		} else if (r[i] == '^') {
			r[i] = '~';
		}
	}
	return r;
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

// RFC2812 §2.3: 1メッセージはCRLF含め512バイト以下．本体は510までに切り詰める
std::string StringUtil::capLine(const std::string& line) {
	if (line.size() > 510) {
		return line.substr(0, 510);
	}
	return line;
}
