#include "commands/Pass.hpp"

#include "Client.hpp"
#include "IrcException.hpp"
#include "Message.hpp"
#include "Reply.hpp"
#include "Server.hpp"

// PASS は登録の一部なので登録前に受け付ける（NICK/USER より先に来る）。
bool PassCommand::needsRegistration() const {
	return false;
}

void PassCommand::execute(Server& server, Client& client, const Message& msg) {
	if (msg.size() == 0 || msg.param(0).empty()) {
		throw IrcException(Reply::ERR_NEEDMOREPARAMS, client.nick(),
				"PASS :Not enough parameters");
	}
	if (msg.param(0) != server.password()) {
		throw IrcException(Reply::ERR_PASSWDMISMATCH, client.nick(),
				":Password incorrect");
	}
	// Client は passwd を保持せず acceptPass() のboolのみ。ここで照合して立てる。
	client.acceptPass();
	server.completeRegistration(client);
}
