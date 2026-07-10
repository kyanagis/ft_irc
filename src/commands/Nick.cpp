#include "commands/Nick.hpp"

#include <string>

#include "Client.hpp"
#include "IrcException.hpp"
#include "Message.hpp"
#include "Reply.hpp"
#include "Server.hpp"

namespace {
	bool isSpecial(char c) {
		return c == '[' || c == ']' || c == '\\' || c == '`'
				|| c == '_' || c == '^' || c == '{' || c == '}' || c == '|';
	}

	bool isLetter(char c) {
		return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
	}

	bool isDigit(char c) {
		return c >= '0' && c <= '9';
	}

	// RFC 2812 §2.3.1: nickname = (letter/special) *8(letter/digit/special/"-")
	// 先頭は letter か special、以降は letter/digit/special/"-"、最大9文字。
	bool isValidNick(const std::string& n) {
		if (n.empty() || n.size() > 9) {
			return false;
		}
		if (!isLetter(n[0]) && !isSpecial(n[0])) {
			return false;
		}
		for (std::string::size_type i = 1; i < n.size(); ++i) {
			char c = n[i];
			if (!isLetter(c) && !isDigit(c) && !isSpecial(c) && c != '-') {
				return false;
			}
		}
		return true;
	}
}

bool NickCommand::needsRegistration() const {
	return false;
}

void NickCommand::execute(Server& server, Client& client, const Message& msg) {
	if (msg.size() == 0 || msg.param(0).empty()) {
		throw IrcException(Reply::ERR_NONICKNAMEGIVEN, client.nick(),
				":No nickname given");
	}

	const std::string& nick = msg.param(0);

	if (!isValidNick(nick)) {
		throw IrcException(Reply::ERR_ERRONEUSNICKNAME, client.nick(),
				nick + " :Erroneous nickname");
	}

	// 自分の今のnickをそのまま再送してきたら何もしない。
	if (client.hasNick() && client.nick() == nick) {
		return;
	}

	// findClientByNick は大文字小文字無視。自分以外が使っていたら 433。
	Client* existing = server.findClientByNick(nick);
	if (existing != 0 && existing != &client) {
		throw IrcException(Reply::ERR_NICKNAMEINUSE, client.nick(),
				nick + " :Nickname is already in use");
	}

	client.setNick(nick);
	server.completeRegistration(client);
}
