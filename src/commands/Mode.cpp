#include "commands/Mode.hpp"

#include <climits>
#include <sstream>
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
	struct ModeChanges {
		std::string flags;
		char        lastSign;
		std::vector<std::string> args;
		ModeChanges() :
			flags(),
			lastSign('\0'),
			args()
		{
		}

		bool any() const {
			return !flags.empty();
		}
	};

	void sendNumeric(Server& server, Client& client, int code, const std::string& detail) {
		server.sendLine(client, Reply::numeric(server.serverName(), code, client.nick(), detail));
	}

	void record(ModeChanges& c, char sign, char flag) {
		if (c.lastSign != sign) {
			c.flags += sign;
			c.lastSign = sign;
		}
		c.flags += flag;
	}

	bool parseLimit(const std::string& s, std::size_t& out) {
		if (s.empty()) {
			return false;
		}
		for (std::string::size_type i = 0; i < s.size(); ++i) {
			if (s[i] < '0' || s[i] > '9') {
				return false;
			}
		}
		std::istringstream iss(s);
		unsigned long v = 0;
		iss >> v;
		if (iss.fail() || !iss.eof()) {
			return false;
		}
		if (v == 0) {
			return false;
		}
		if (v > static_cast<unsigned long>(LONG_MAX)) {   // long キャストで負数化するのを防ぐ
			return false;
		}
		out = static_cast<std::size_t>(v);
		return true;
	}

	bool isValidKey(const std::string& key) {
		if (key.empty())
			return false;
		if (key[0] == ':')
			return false;
		for (std::string::size_type i = 0; i < key.size(); ++i) {
			char c = key[i];
			if (c == ' ' || c == ',' || c == '\r' || c == '\n'
					|| c == '\0' || c == '\f') {
				return false;
			}
		}
		return true;
	}

	enum ModeCheck { MODE_OK, MODE_NEED_PARAMS, MODE_BAD_KEY };

	ModeCheck validateModes(const std::string& modestr, const Message& msg) {
		// 引数キューは index 2..msg.size()-1。引数不足と +k 鍵内容不正を事前検出する。
		std::size_t cur = 2;
		std::size_t argCount = msg.size();
		char sign = '+';
		for (std::string::size_type i = 0; i < modestr.size(); ++i) {
			char c = modestr[i];
			if (c == '+' || c == '-') {
				sign = c;
				continue;
			}
			switch (c) {
			case 'i': case 't':
				break;                              // 引数消費なし
			case 'k':                               // +k/-k とも必須1引数、+k は鍵内容も検証
				if (cur >= argCount)
					return MODE_NEED_PARAMS;
				if (sign == '+' && !isValidKey(msg.param(cur)))
					return MODE_BAD_KEY;
				++cur;
				break;
			case 'l':                               // +l は必須1、-l は消費なし
				if (sign == '+') {
					if (cur >= argCount)
						return MODE_NEED_PARAMS;
					++cur;
				}
				break;
			case 'o':                               // +o/-o とも必須1
				if (cur >= argCount)
					return MODE_NEED_PARAMS;
				++cur;
				break;
			default:
				break;                              // unknown mode は引数消費なし（472は適用側）
			}
		}
		return MODE_OK;
	}

	void applyOne(Server& server, Client& client, Channel& channel,
			char sign, char flag, const Message& msg,
			std::size_t& argIdx, ModeChanges& changes) {
		switch (flag) {
		case 'i':
			if (sign == '+' && !channel.inviteOnly()) {
				channel.setInviteOnly(true);
				record(changes, '+', 'i');
			} else if (sign == '-' && channel.inviteOnly()) {
				channel.setInviteOnly(false);
				record(changes, '-', 'i');
			}
			return;
		case 't':
			if (sign == '+' && !channel.topicLocked()) {
				channel.setTopicLocked(true);
				record(changes, '+', 't');
			} else if (sign == '-' && channel.topicLocked()) {
				channel.setTopicLocked(false);
				record(changes, '-', 't');
			}
			return;
		case 'k':
			if (sign == '+') {
				std::string arg = msg.param(argIdx);
				++argIdx;
				if (!channel.hasKey() || channel.key() != arg) {
					channel.setKey(arg);
					record(changes, '+', 'k');
					changes.args.push_back(arg);
				}
			} else {  // -k : 引数は消費するが値は使わない。通知には解除前の実鍵をechoする。
				++argIdx;
				if (channel.hasKey()) {
					changes.args.push_back(channel.key());   // 解除前の実鍵（検証済みで安全）
					channel.clearKey();
					record(changes, '-', 'k');
				}
				// key未設定なら no-op（arg は消費済み、record も push もしない）
			}
			return;
		case 'l':
			if (sign == '+') {
				std::string arg = msg.param(argIdx);
				++argIdx;
				std::size_t n;
				if (!parseLimit(arg, n)) {
					return;
				}
				if (!channel.hasLimit() || channel.limit() != n) {
					channel.setLimit(n);
					record(changes, '+', 'l');
					changes.args.push_back(
							StringUtil::toString(static_cast<long>(n)));
				}
			} else {
				if (channel.hasLimit()) {
					channel.clearLimit();
					record(changes, '-', 'l');
				}
			}
			return;
		case 'o': {
			std::string nick = msg.param(argIdx);
			++argIdx;
			Client* tgt = server.findClientByNick(nick);
			if (tgt == 0 || !channel.hasMember(*tgt)) {
				sendNumeric(server, client, Reply::ERR_USERNOTINCHANNEL,
						nick + " " + channel.name()
						+ " :They aren't on that channel");
				return;
			}
			if (sign == '+') {
				if (!channel.isOperator(*tgt)) {
					channel.addOperator(*tgt);
					record(changes, '+', 'o');
					changes.args.push_back(nick);
				}
			} else {
				if (channel.isOperator(*tgt)) {
					channel.removeOperator(*tgt);
					record(changes, '-', 'o');
					changes.args.push_back(nick);
				}
			}
			return;
		}
		default:
			sendNumeric(server, client, Reply::ERR_UNKNOWNMODE,
					std::string(1, flag) + " :is unknown mode char to me");
			return;
		}
	}

	std::string buildAppliedString(const ModeChanges& c) {
		std::string result = c.flags;
		for (std::size_t i = 0; i < c.args.size(); ++i) {
			result += " " + c.args[i];
		}
		return result;
	}
}

bool ModeCommand::needsRegistration() const {
	return true;
}

void ModeCommand::execute(Server& server, Client& client, const Message& msg) {
	if (msg.size() == 0 || msg.param(0).empty()) {
		throw IrcException(Reply::ERR_NEEDMOREPARAMS, client.nick(),
				"MODE :Not enough parameters");
	}

	const std::string& target = msg.param(0);
	if (target[0] != '#') {
		return;   // user-mode: ignore
	}

	Channel* channel = server.findChannel(target);
	if (channel == 0) {
		throw IrcException(Reply::ERR_NOSUCHCHANNEL, client.nick(),
				target + " :No such channel");
	}

	if (msg.size() < 2) {
		sendNumeric(server, client, Reply::RPL_CHANNELMODEIS,
				channel->name() + " " + channel->modeString());
		return;
	}

	if (!channel->isOperator(client)) {
		throw IrcException(Reply::ERR_CHANOPRIVSNEEDED, client.nick(),
				channel->name() + " :You're not channel operator");
	}

	const std::string& modestr = msg.param(1);
	ModeCheck check = validateModes(modestr, msg);
	if (check == MODE_NEED_PARAMS) {
		throw IrcException(Reply::ERR_NEEDMOREPARAMS, client.nick(),
				channel->name() + " :Not enough parameters");
	} else if (check == MODE_BAD_KEY) {
		throw IrcException(Reply::ERR_NEEDMOREPARAMS, client.nick(),
				channel->name() + " :Invalid channel key");
	}
	ModeChanges changes;
	std::size_t argIdx = 2;
	char sign = '+';
	for (std::string::size_type i = 0; i < modestr.size(); ++i) {
		char c = modestr[i];
		if (c == '+' || c == '-') {
			sign = c;
			continue;
		}
		applyOne(server, client, *channel, sign, c, msg, argIdx, changes);
	}

	if (changes.any()) {
		channel->broadcast(Reply::from(client.prefix(),
				"MODE " + channel->name() + " " + buildAppliedString(changes)));
	}
}
