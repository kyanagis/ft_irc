#ifndef IRC_EXCEPTION_HPP
#define IRC_EXCEPTION_HPP

#include <stdexcept>
#include <string>

class IrcException : public std::runtime_error
{
public:
	IrcException(int code, const std::string& target, const std::string& detail);
	~IrcException() throw();

	int                code() const throw();
	const std::string& target() const throw();
	const std::string& detail() const throw();

private:
	int         _code;
	std::string _target;
	std::string _detail;
};

#endif
