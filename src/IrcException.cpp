#include "IrcException.hpp"

IrcException::IrcException(int code, const std::string& target,
		const std::string& detail)
		: std::runtime_error(detail),
			_code(code),
			_target(target),
			_detail(detail) {
}

IrcException::~IrcException() throw() {
}

int IrcException::code() const throw() {
	return _code;
}

const std::string& IrcException::target() const throw() {
	return _target;
}

const std::string& IrcException::detail() const throw() {
	return _detail;
}
