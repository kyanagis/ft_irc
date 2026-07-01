#ifndef MESSAGE_HPP
#define MESSAGE_HPP

#include <string>
#include <vector>
#include <cstddef>

class Message
{
public:
	static Message parse(const std::string& line);

	bool                            empty() const;
	const std::string&              prefix() const;
	const std::string&              command() const;
	const std::vector<std::string>& params() const;
	const std::string&              param(std::size_t index) const;
	std::size_t                     size() const;

private:
	Message();

	std::string              _prefix;
	std::string              _command;
	std::vector<std::string> _params;
};

#endif
