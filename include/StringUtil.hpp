#ifndef STRING_UTIL_H
#define STRING_UTIL_H

#include <string>
#include <vector>

class StringUtil
{
public:
	static std::string              toUpper(const std::string& s);
	static std::string              toLower(const std::string& s);
	static std::string              trim(const std::string& s);
	static std::vector<std::string> split(const std::string& s, char delim);
	static std::string              toString(long value);

private:
	StringUtil();
	StringUtil(const StringUtil&);
	StringUtil& operator=(const StringUtil&);
};

#endif
