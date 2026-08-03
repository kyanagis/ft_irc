#include "commands/Part.hpp"

#include <string>
#include <vector>

#include "Channel.hpp"
#include "Client.hpp"
#include "IrcException.hpp"
#include "Log.hpp"
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

	void partOneChannel(Server& server, Client& client,
			const std::string& name, const std::string& reason,
			bool appendReason) {
		Channel* channel = server.findChannel(name);
		if (channel == 0) {
			sendNumeric(server, client, Reply::ERR_NOSUCHCHANNEL,
					name + " :No such channel");
			return;
		}
		if (!channel->hasMember(client)) {
			sendNumeric(server, client, Reply::ERR_NOTONCHANNEL,
					channel->name() + " :You're not on that channel");
			return;
		}

		std::string body = "PART " + channel->name();
		if (appendReason) {
			body += " :" + reason;
		}
		std::string line = Reply::from(client.prefix(), body);

		channel->broadcast(line);
		channel->removeMember(client);
		Log::memb("< " + client.nick() + " left " + channel->name()
				+ (appendReason ? " (" + reason + ")" : std::string())
				+ ", " + StringUtil::toString(
						static_cast<long>(channel->memberCount()))
				+ " remaining");
		server.removeEmptyChannel(channel);
	}
}

bool PartCommand::needsRegistration() const {
	return true;
}

void PartCommand::execute(Server& server, Client& client, const Message& msg) {
	if (msg.size() == 0 || msg.param(0).empty()) {
		throw IrcException(Reply::ERR_NEEDMOREPARAMS, client.nick(),
				"PART :Not enough parameters");
	}

	std::vector<std::string> chans = StringUtil::split(msg.param(0), ',');
	const std::string& reason = msg.param(1);
	bool appendReason = !reason.empty();

	for (std::size_t i = 0; i < chans.size(); ++i) {
		if (chans[i].empty()) {
			continue;
		}
		partOneChannel(server, client, chans[i], reason, appendReason);
	}
}
