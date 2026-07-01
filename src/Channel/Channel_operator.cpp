#include "Channel.hpp"
#include "Client.hpp"

bool Channel::isOperator(Client& client) const {
	return _operators.find(&client) != _operators.end();
}

void Channel::addOperator(Client& client) {
	_operators.insert(&client);
}

void Channel::removeOperator(Client& client) {
	_operators.erase(&client);
}
