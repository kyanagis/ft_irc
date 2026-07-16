#include "commands/Kick.hpp"

#include <string>
#include <vector>

#include "Channel.hpp"
#include "Client.hpp"
#include "IrcException.hpp"
#include "Message.hpp"
#include "Reply.hpp"
#include "Server.hpp"
#include "StringUtil.hpp"

namespace {
	void sendNumeric(Server& server, Client& client, int code,
			const std::string& detail) {
		server.sendLine(client, Reply::numeric(server.serverName(), code,
				client.nick(), detail));
	}

	void kickOneTarget(Server& server, Client& client, Channel& channel,
			const std::string& token, const std::string& comment) {
		Client* target = server.findClientByNick(token);
		if (target == 0 || !channel.hasMember(*target)) {
			sendNumeric(server, client, Reply::ERR_USERNOTINCHANNEL,
					token + " " + channel.name() + " :They aren't on that channel");
			return;
		}
		std::string line = Reply::from(client.prefix(),
				"KICK " + channel.name() + " " + token + " :" + comment);
		channel.broadcast(line);
		channel.removeMember(*target);
	}
}

bool KickCommand::needsRegistration() const {
	return true;
}

void KickCommand::execute(Server& server, Client& client, const Message& msg) {
	if (msg.size() < 2) {
		throw IrcException(Reply::ERR_NEEDMOREPARAMS, client.nick(),
				"KICK :Not enough parameters");
	}
	Channel* channel = server.findChannel(msg.param(0));
	if (channel == 0) {
		throw IrcException(Reply::ERR_NOSUCHCHANNEL, client.nick(),
				msg.param(0) + " :No such channel");
	}
	if (!channel->hasMember(client)) {
		throw IrcException(Reply::ERR_NOTONCHANNEL, client.nick(),
				channel->name() + " :You're not on that channel");
	}
	if (!channel->isOperator(client)) {
		throw IrcException(Reply::ERR_CHANOPRIVSNEEDED, client.nick(),
				channel->name() + " :You're not channel operator");
	}

	std::string comment = client.nick();
	if (msg.size() >= 3) {
		comment = msg.param(2);
	}
	std::vector<std::string> targets = StringUtil::split(msg.param(1), ',');

	for (std::size_t i = 0; i < targets.size(); ++i) {
		if (targets[i].empty()) {
			continue;
		}
		kickOneTarget(server, client, *channel, targets[i], comment);
	}

	server.removeEmptyChannel(channel);
}
