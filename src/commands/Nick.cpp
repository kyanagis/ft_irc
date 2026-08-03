#include "commands/Nick.hpp"

#include <set>
#include <string>

#include "Channel.hpp"
#include "Client.hpp"
#include "IrcException.hpp"
#include "Log.hpp"
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

	void notifyNickChange(Server& server, Client& client,
			const std::string& newNick) {
		const std::string line = Reply::from(client.prefix(), "NICK :" + newNick);

		std::set<Client*> recipients;
		recipients.insert(&client);
		const std::set<std::string>& chans = client.channels();
		for (std::set<std::string>::const_iterator it = chans.begin();
				it != chans.end(); ++it) {
			Channel* channel = server.findChannel(*it);
			if (channel != 0) {
				const std::set<Client*>& members = channel->members();
				recipients.insert(members.begin(), members.end());
			}
		}
		for (std::set<Client*>::iterator it = recipients.begin();
				it != recipients.end(); ++it) {
			server.sendLine(**it, line);
		}
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

	if (client.hasNick() && client.nick() == nick) {
		return;
	}

	Client* existing = server.findClientByNick(nick);
	if (existing != 0 && existing != &client) {
		throw IrcException(Reply::ERR_NICKNAMEINUSE, client.nick(),
				nick + " :Nickname is already in use");
	}

	if (client.isRegistered()) {
		notifyNickChange(server, client, nick);
		Log::auth("* " + client.nick() + " is now known as " + nick);
		client.setNick(nick);
	} else {
		client.setNick(nick);
		server.completeRegistration(client);
	}
}
