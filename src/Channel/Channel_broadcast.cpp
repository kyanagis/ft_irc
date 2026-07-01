#include "Channel.hpp"
#include "Client.hpp"

void Channel::broadcast(const std::string& message, Client* except) {
	for (std::set<Client*>::const_iterator it = _members.begin();
	     it != _members.end(); ++it) {
		if (*it == except)
			continue;
		(*it)->appendOutput(message);
	}
}
