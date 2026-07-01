#include "Channel.hpp"
#include "Client.hpp"

void Channel::addMember(Client& client) {
	_members.insert(&client);
	client.joinChannel(_name);
}

void Channel::removeMember(Client& client) {
	_members.erase(&client);
	_operators.erase(&client);
	_invited.erase(&client);
	client.leaveChannel(_name);
}

bool Channel::hasMember(Client& client) const {
	return _members.find(&client) != _members.end();
}

bool Channel::isEmpty() const {
	return _members.empty();
}

std::size_t Channel::memberCount() const {
	return _members.size();
}

const std::set<Client*>& Channel::members() const {
	return _members;
}
