#include "commands/Who.hpp"

#include <set>
#include <string>

#include "Channel.hpp"
#include "Client.hpp"
#include "Message.hpp"
#include "Reply.hpp"
#include "Server.hpp"

namespace {
	void sendNumeric(Server& server, Client& client, int code,
			const std::string& detail) {
		server.sendLine(client, Reply::numeric(server.serverName(), code,
				client.nick(), detail));
	}

	void sendWhoReply(Server& server, Client& client,
			const std::string& channelName, const Client& target, bool isOp) {
		std::string detail = channelName
				+ " " + target.user()
				+ " " + target.host()
				+ " " + server.serverName()
				+ " " + target.nick()
				+ " H";
		if (isOp) {
			detail += "@";
		}
		detail += " :0 " + target.realname();
		sendNumeric(server, client, Reply::RPL_WHOREPLY, detail);
	}
}

bool WhoCommand::needsRegistration() const {
	return true;
}

void WhoCommand::execute(Server& server, Client& client, const Message& msg) {
	const std::string& mask = msg.param(0);

	if (!mask.empty() && mask[0] == '#') {
		Channel* channel = server.findChannel(mask);
		if (channel != 0) {
			const std::set<Client*>& members = channel->members();
			for (std::set<Client*>::const_iterator it = members.begin();
					it != members.end(); ++it) {
				sendWhoReply(server, client, channel->name(), **it,
						channel->isOperator(**it));
			}
		}
	} else if (!mask.empty()) {
		Client* target = server.findClientByNick(mask);
		if (target != 0 && target->isRegistered()) {
			sendWhoReply(server, client, "*", *target, false);
		}
	}

	sendNumeric(server, client, Reply::RPL_ENDOFWHO,
			(mask.empty() ? std::string("*") : mask) + " :End of WHO list");
}
