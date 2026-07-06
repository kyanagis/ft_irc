#include "Channel.hpp"
#include "StringUtil.hpp"

bool Channel::inviteOnly() const {
	return _inviteOnly;
}

void Channel::setInviteOnly(bool on) {
	_inviteOnly = on;
}

bool Channel::topicLocked() const {
	return _topicLocked;
}

void Channel::setTopicLocked(bool on) {
	_topicLocked = on;
}

bool Channel::hasKey() const {
	return _hasKey;
}

const std::string& Channel::key() const {
	return _key;
}

void Channel::setKey(const std::string& key) {
	_key = key;
	_hasKey = true;
}

void Channel::clearKey() {
	_key.clear();
	_hasKey = false;
}

bool Channel::hasLimit() const {
	return _hasLimit;
}

std::size_t Channel::limit() const {
	return _limit;
}

void Channel::setLimit(std::size_t limit) {
	_limit = limit;
	_hasLimit = true;
}

void Channel::clearLimit() {
	_limit = 0;
	_hasLimit = false;
}

std::string Channel::modeString() const {
	std::string flags = "+";
	std::string params;

	if (_inviteOnly)
		flags += "i";
	if (_topicLocked)
		flags += "t";
	if (_hasKey) {
		flags += "k";
		params += " " + _key;
	}
	if (_hasLimit) {
		flags += "l";
		params += " " + StringUtil::toString(static_cast<long>(_limit));
	}
	return flags + params;
}
