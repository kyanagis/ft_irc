#include "commands/Join.hpp"

#include <set>
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

	bool isValidChannelName(const std::string& name) {
		if (name.empty() || name[0] != '#' || name.size() > 50) {
			return false;
		}
		for (std::string::size_type i = 0; i < name.size(); ++i) {
			char c = name[i];
			if (c == ' ' || c == ',' || c == '\a' || c == '\r'
					|| c == '\n' || c == '\0') {
				return false;
			}
		}
		return true;
	}

	bool canJoin(Server& server, Client& client, Channel& channel,
			const std::string& key) {
		if (channel.hasKey() && key != channel.key()) {
			sendNumeric(server, client, Reply::ERR_BADCHANNELKEY,
					channel.name() + " :Cannot join channel (+k)");
			return false;
		}
		if (channel.inviteOnly() && !channel.isInvited(client)) {
			sendNumeric(server, client, Reply::ERR_INVITEONLYCHAN,
					channel.name() + " :Cannot join channel (+i)");
			return false;
		}
		if (channel.hasLimit() && channel.memberCount() >= channel.limit()) {
			sendNumeric(server, client, Reply::ERR_CHANNELISFULL,
					channel.name() + " :Cannot join channel (+l)");
			return false;
		}
		return true;
	}

	// RPL_NAMREPLY(353) を、各 numeric 行が512(CRLF含む)以下に収まるよう複数の detail に分割する。
	// 1行に全員詰めると多人数チャンネルで512超→末尾が切れてメンバーが欠落するため。
	std::vector<std::string> buildNameReplies(Server& server, Client& client,
			Channel& channel) {
		const std::string head = "= " + channel.name() + " :";
		const std::string::size_type lineMax = 510;  // 512 - CRLF
		const std::string::size_type overhead = Reply::numeric(
				server.serverName(), Reply::RPL_NAMREPLY, client.nick(), head).size();
		const std::string::size_type budget = (lineMax > overhead) ? lineMax - overhead : 1;

		std::vector<std::string> lines;
		std::string names;
		const std::set<Client*>& members = channel.members();
		for (std::set<Client*>::const_iterator it = members.begin();
		     it != members.end(); ++it) {
			std::string tok = (*it)->nick();
			if (channel.isOperator(**it))
				tok = "@" + tok;
			std::string::size_type addLen = names.empty() ? tok.size() : tok.size() + 1;
			if (!names.empty() && names.size() + addLen > budget) {
				lines.push_back(head + names);
				names = tok;
			} else {
				if (!names.empty())
					names += " ";
				names += tok;
			}
		}
		lines.push_back(head + names);
		return lines;
	}

	void sendJoinReplies(Server& server, Client& joiner, Channel& channel) {
		channel.broadcast(Reply::from(joiner.prefix(), "JOIN " + channel.name()));

		if (channel.hasTopic()) {
			sendNumeric(server, joiner, Reply::RPL_TOPIC,
					channel.name() + " :" + channel.topic());
		} else {
			sendNumeric(server, joiner, Reply::RPL_NOTOPIC,
					channel.name() + " :No topic is set");
		}

		std::vector<std::string> nameLines = buildNameReplies(server, joiner, channel);
		for (std::size_t i = 0; i < nameLines.size(); ++i) {
			sendNumeric(server, joiner, Reply::RPL_NAMREPLY, nameLines[i]);
		}
		sendNumeric(server, joiner, Reply::RPL_ENDOFNAMES,
				channel.name() + " :End of /NAMES list");
	}

	void joinOneChannel(Server& server, Client& client,
			const std::string& name, const std::string& key) {
		if (!isValidChannelName(name)) {
			sendNumeric(server, client, Reply::ERR_NOSUCHCHANNEL,
					name + " :No such channel");
			return;
		}

		Channel* channel = server.findChannel(name);
		if (channel == 0) {
			// 新規作成: コンストラクタが作成者を member+operator に登録する。
			sendJoinReplies(server, client,
					*server.getOrCreateChannel(name, client));
			return;
		}

		if (channel->hasMember(client)) {
			return;   // 既メンバは無視（RFC 2812 §3.2.1）
		}
		if (!canJoin(server, client, *channel, key)) {
			return;
		}
		channel->addMember(client);
		channel->clearInvite(client);
		sendJoinReplies(server, client, *channel);
	}
}

bool JoinCommand::needsRegistration() const {
	return true;
}

void JoinCommand::execute(Server& server, Client& client, const Message& msg) {
	if (msg.size() == 0 || msg.param(0).empty()) {
		throw IrcException(Reply::ERR_NEEDMOREPARAMS, client.nick(),
				"JOIN :Not enough parameters");
	}

	std::vector<std::string> chans = StringUtil::split(msg.param(0), ',');
	std::vector<std::string> keys;
	if (msg.size() >= 2) {
		keys = StringUtil::split(msg.param(1), ',');
	}

	for (std::size_t i = 0; i < chans.size(); ++i) {
		if (chans[i].empty()) {
			continue;
		}
		std::string key = (i < keys.size()) ? keys[i] : std::string();
		joinOneChannel(server, client, chans[i], key);
	}
}
