#include "Channel.hpp"
#include "Client.hpp"

void Channel::invite(Client& client) {
	_invited.insert(&client);
}

bool Channel::isInvited(Client& client) const {
	return _invited.find(&client) != _invited.end();
}

void Channel::clearInvite(Client& client) {
	_invited.erase(&client);
}
