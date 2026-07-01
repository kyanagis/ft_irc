#include "Channel.hpp"
#include "Client.hpp"

Channel::Channel(const std::string& name, Client& creator)
	: _name(name),
	  _hasTopic(false),
	  _inviteOnly(false),
	  _topicLocked(false),
	  _hasKey(false),
	  _hasLimit(false),
	  _limit(0)
{
	_members.insert(&creator);
	_operators.insert(&creator);
	creator.joinChannel(_name);
}

const std::string& Channel::name() const {
	return _name;
}
