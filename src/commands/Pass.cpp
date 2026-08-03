#include "commands/Pass.hpp"

#include "Client.hpp"
#include "IrcException.hpp"
#include "Message.hpp"
#include "Reply.hpp"
#include "Server.hpp"

bool PassCommand::needsRegistration() const {
	return false;
}

void PassCommand::execute(Server& server, Client& client, const Message& msg) {
	if (client.isRegistered()) {
		throw IrcException(Reply::ERR_ALREADYREGISTRED, client.nick(),
				":You may not reregister");
	}
	if (msg.size() == 0 || msg.param(0).empty()) {
		throw IrcException(Reply::ERR_NEEDMOREPARAMS, client.nick(),
				"PASS :Not enough parameters");
	}
	if (msg.param(0) != server.password()) {
		throw IrcException(Reply::ERR_PASSWDMISMATCH, client.nick(),
				":Password incorrect");
	}
	client.acceptPass();
	server.completeRegistration(client);
}
