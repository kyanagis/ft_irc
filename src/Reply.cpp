#include "Reply.hpp"

#include <iomanip>
#include <sstream>

std::string Reply::numeric(const std::string& server, int code,
		const std::string& target, const std::string& rest) {
	std::ostringstream oss;
	oss << ':' << server << ' '
			<< std::setw(3) << std::setfill('0') << code << ' ';
	if (target.empty()) {
		oss << '*';
	} else {
		oss << target;
	}
	if (!rest.empty()) {
		oss << ' ' << rest;
	}
	return oss.str();
}

std::string Reply::from(const std::string& prefix, const std::string& body) {
	return ":" + prefix + " " + body;
}
