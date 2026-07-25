#include "commands/User.hpp"

#include "Client.hpp"
#include "IrcException.hpp"
#include "Message.hpp"
#include "Reply.hpp"
#include "Server.hpp"

namespace {
	// RFC2812 §2.3.1: user は NUL/CR/LF/SPACE/'@' 以外の任意オクテット1文字以上．
	// '@' を通すと prefix が nick!bad@user@host になり user と host の境界が壊れる．
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
	// 黙って除去すると本人の知らない identity になるので 461 で拒否．未登録のまま
	// なので正しい USER を再送すれば登録できる．
	if (!isValidUser(msg.param(0))) {
		throw IrcException(Reply::ERR_NEEDMOREPARAMS, client.nick(),
				"USER :Invalid username");
	}
	client.setUser(msg.param(0), msg.param(3));
	server.completeRegistration(client);
}
