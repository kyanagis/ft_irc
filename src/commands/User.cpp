#include "commands/User.hpp"

#include "Client.hpp"
#include "IrcException.hpp"
#include "Message.hpp"
#include "Reply.hpp"
#include "Server.hpp"

namespace {
	bool isValidUser(const std::string& u) {
		if (u.empty()) {
			return false;
		}
		for (std::string::size_type i = 0; i < u.size(); ++i) {
			char c = u[i];
			if (c == '\0' || c == '\r' || c == '\n' || c == ' ' || c == '@') {
				return false;
			}
		}
		return true;
	}
}

bool UserCommand::needsRegistration() const {
	return false;
}

void UserCommand::execute(Server& server, Client& client, const Message& msg) {
	if (client.isRegistered()) {
		throw IrcException(Reply::ERR_ALREADYREGISTRED, client.nick(),
				":You may not reregister");
	}
	if (msg.size() < 4) {
		throw IrcException(Reply::ERR_NEEDMOREPARAMS, client.nick(),
				"USER :Not enough parameters");
	}
	if (!isValidUser(msg.param(0))) {
		throw IrcException(Reply::ERR_NEEDMOREPARAMS, client.nick(),
				"USER :Invalid username");
	}
	client.setUser(msg.param(0), msg.param(3));
	server.completeRegistration(client);
}
