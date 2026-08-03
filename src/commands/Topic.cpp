#include "commands/Topic.hpp"

#include <string>

#include "Channel.hpp"
#include "Client.hpp"
#include "IrcException.hpp"
#include "Log.hpp"
#include "Message.hpp"
#include "Reply.hpp"
#include "Server.hpp"

namespace {
	void sendNumeric(Server& server, Client& client, int code,
			const std::string& detail) {
		server.sendLine(client, Reply::numeric(server.serverName(), code,
				client.nick(), detail));
	}

	std::string preview(const std::string& s) {
		if (s.size() <= 60) {
			return s;
		}
		return s.substr(0, 57) + "...";
	}
}

bool TopicCommand::needsRegistration() const {
	return true;
}

void TopicCommand::execute(Server& server, Client& client, const Message& msg) {
	if (msg.size() == 0 || msg.param(0).empty()) {
		throw IrcException(Reply::ERR_NEEDMOREPARAMS, client.nick(),
				"TOPIC :Not enough parameters");
	}

	const std::string& requested = msg.param(0);
	Channel* channel = server.findChannel(requested);
	if (channel == 0) {
		throw IrcException(Reply::ERR_NOSUCHCHANNEL, client.nick(),
				requested + " :No such channel");
	}
	if (!channel->hasMember(client)) {
		throw IrcException(Reply::ERR_NOTONCHANNEL, client.nick(),
				channel->name() + " :You're not on that channel");
	}

	if (msg.size() < 2) {
		if (channel->hasTopic()) {
			sendNumeric(server, client, Reply::RPL_TOPIC,
					channel->name() + " :" + channel->topic());
		} else {
			sendNumeric(server, client, Reply::RPL_NOTOPIC,
					channel->name() + " :No topic is set");
		}
		return;
	}

	if (channel->topicLocked() && !channel->isOperator(client)) {
		throw IrcException(Reply::ERR_CHANOPRIVSNEEDED, client.nick(),
				channel->name() + " :You're not channel operator");
	}

	channel->setTopic(msg.param(1), client.nick());
	channel->broadcast(Reply::from(client.prefix(),
			"TOPIC " + channel->name() + " :" + msg.param(1)));
	Log::mode("@ " + client.nick() + " set topic on " + channel->name()
			+ ": \"" + preview(msg.param(1)) + "\"");
}
