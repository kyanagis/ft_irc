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

// 324応答の本文を組み立てる。非メンバには鍵・上限の値を '*' でマスクする(RFC2811 §4.2.9/§4.2.10)
std::string Channel::modeString(Client& viewer) const {
	bool viewerIsMember = hasMember(viewer);
	std::string flags = "+";
	std::string params;

	if (_inviteOnly == true)
		flags += "i";
	if (_topicLocked == true)
		flags += "t";
	if (_hasKey == true) {
		flags += "k";
		if (viewerIsMember == true)
			params += " " + _key;
		else
			params += " *";
	}
	if (_hasLimit == true) {
		flags += "l";
		if (viewerIsMember == true)
			params += " " + StringUtil::toString(static_cast<long>(_limit));
		else
			params += " *";
	}
	return flags + params;
}
