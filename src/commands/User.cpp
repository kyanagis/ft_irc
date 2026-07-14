#include "commands/User.hpp"

#include "Client.hpp"
#include "IrcException.hpp"
#include "Message.hpp"
#include "Reply.hpp"
#include "Server.hpp"

// USER も登録の一部なので登録前に受け付ける。
bool UserCommand::needsRegistration() const {
	return false;
}

void UserCommand::execute(Server& server, Client& client, const Message& msg) {
	if (client.isRegistered()) {
		throw IrcException(Reply::ERR_ALREADYREGISTRED, client.nick(),
				":You may not reregister");
	}
	// USER <username> <mode> <unused> :<realname> の4引数が必須。
	// 中2つ（mode/unused）はRFC上無視する。
	if (msg.size() < 4) {
		throw IrcException(Reply::ERR_NEEDMOREPARAMS, client.nick(),
				"USER :Not enough parameters");
	}
	client.setUser(msg.param(0), msg.param(3));
	server.completeRegistration(client);
}
