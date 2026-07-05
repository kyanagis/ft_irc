#include "Channel.hpp"
#include "Client.hpp"

const std::string& Channel::topic() const {
	return _topic;
}

const std::string& Channel::topicSetter() const {
	return _topicSetter;
}

bool Channel::hasTopic() const {
	return _hasTopic;
}

void Channel::setTopic(const std::string& topic, const std::string& setterNick) {
	_topic = topic;
	_topicSetter = setterNick;
	_hasTopic = !topic.empty();
}
