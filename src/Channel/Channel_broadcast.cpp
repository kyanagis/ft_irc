#include "Channel.hpp"
#include "Client.hpp"
#include "StringUtil.hpp"

void Channel::broadcast(const std::string& message, Client* except) {
	const std::string line = StringUtil::capLine(message) + "\r\n";
	for (std::set<Client*>::const_iterator it = _members.begin();
	     it != _members.end(); ++it) {
		if (*it == except)
			continue;
		(*it)->appendOutput(line);
	}
}
