#include "commands/Mode.hpp"

#include <climits>
#include <sstream>
#include <string>
#include <vector>

#include "Channel.hpp"
#include "Client.hpp"
#include "IrcException.hpp"
#include "Log.hpp"
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
		if (v > static_cast<unsigned long>(LONG_MAX)) {
			return false;
		}
		out = static_cast<std::size_t>(v);
		return true;
	}

	bool isValidKey(const std::string& key) {
		if (key.empty() == true || key.size() > 23)
			return false;
		if (key[0] == ':')
			return false;
		for (std::string::size_type i = 0; i < key.size(); ++i) {
			unsigned char uc = static_cast<unsigned char>(key[i]);
			if (uc == '\0' || uc > 0x7F)
				return false;
			if (uc == '\t' || uc == '\n' || uc == '\v' || uc == '\r' || uc == ' ' || uc == 0x06)
				return false;
			if (uc == ',')
				return false;
		}
		return true;
	}

	typedef bool (*ModeValidateFn)(char sign, const std::string& arg);

	bool validateKey(char sign, const std::string& arg) {
		if (sign == '-')
			return true;
		return isValidKey(arg);
	}

	bool validateLimit(char sign, const std::string& arg) {
		(void)sign;
		std::size_t dummy;
		return parseLimit(arg, dummy);
	}

	struct ModeSpec {
		ModeValidateFn validate;
		const char*    badArgDetail;
		int            badArgReply;
		char           flag;
		bool           argOnSet;
		bool           argOnUnset;
	};

	static const ModeSpec kModeTable[] = {
		{ 0,              0,                      0,                         'i', false,   false },
		{ 0,              0,                      0,                         't', false,   false },
		{ &validateKey,   ":Invalid channel key", Reply::ERR_NEEDMOREPARAMS, 'k', true,    false },
		{ &validateLimit, 0,                      0,                         'l', true,    false },
		{ 0,              0,                      0,                         'o', true,    true  },
	};
	static const std::size_t kModeTableSize = sizeof(kModeTable) / sizeof(kModeTable[0]);

	static const std::size_t MAX_PARAM_MODES = 3;

	const ModeSpec* findSpec(char flag) {
		for (std::size_t i = 0; i < kModeTableSize; ++i)
			if (kModeTable[i].flag == flag)
				return &kModeTable[i];
		return 0;
	}

	bool consumesArg(const ModeSpec* spec, char sign) {
		if (spec == 0)
			return false;
		if (sign == '+')
			return spec->argOnSet;
		return spec->argOnUnset;
	}

	struct ModeStep {
		bool            isSign;
		const ModeSpec* spec;
		bool            takesArg;
		std::string     arg;
		bool            argMissing;
	};

	ModeStep nextModeStep(char c, char& sign, const Message& msg, std::size_t& argIdx) {
		ModeStep st;
		st.isSign = false;
		st.spec = 0;
		st.takesArg = false;
		st.argMissing = false;
		if (c == '+' || c == '-') {
			sign = c;
			st.isSign = true;
			return st;
		}
		st.spec = findSpec(c);
		st.takesArg = consumesArg(st.spec, sign);
		if (st.takesArg == true) {
			if (argIdx >= msg.size())
				st.argMissing = true;
			else {
				st.arg = msg.param(argIdx);
				++argIdx;
			}
		}
		return st;
	}

	void applyOne(Server& server, Client& client, Channel& channel,
			char sign, char flag, const std::string& arg, ModeChanges& changes) {
		switch (flag) {
		case 'i':
			if (sign == '+' && channel.inviteOnly() == false) {
				channel.setInviteOnly(true);
				record(changes, '+', 'i');
			} else if (sign == '-' && channel.inviteOnly() == true) {
				channel.setInviteOnly(false);
				record(changes, '-', 'i');
			}
			return;
		case 't':
			if (sign == '+' && channel.topicLocked() == false) {
				channel.setTopicLocked(true);
				record(changes, '+', 't');
			} else if (sign == '-' && channel.topicLocked() == true) {
				channel.setTopicLocked(false);
				record(changes, '-', 't');
			}
			return;
		case 'k':
			if (sign == '+') {
				if (channel.hasKey() == false || channel.key() != arg) {
					channel.setKey(arg);
					record(changes, '+', 'k');
					changes.args.push_back(arg);
				}
			} else {
				if (channel.hasKey() == true) {
					changes.args.push_back(channel.key());
					channel.clearKey();
					record(changes, '-', 'k');
				}
			}
			return;
		case 'l':
			if (sign == '+') {
				std::size_t n;
				if (parseLimit(arg, n) == false) {
					return;
				}
				if (channel.hasLimit() == false || channel.limit() != n) {
					channel.setLimit(n);
					record(changes, '+', 'l');
					changes.args.push_back(
							StringUtil::toString(static_cast<long>(n)));
				}
			} else {
				if (channel.hasLimit() == true) {
					channel.clearLimit();
					record(changes, '-', 'l');
				}
			}
			return;
		case 'o': {
			Client* tgt = server.findClientByNick(arg);
			if (tgt == 0 || channel.hasMember(*tgt) == false) {
				sendNumeric(server, client, Reply::ERR_USERNOTINCHANNEL,
						arg + " " + channel.name()
						+ " :They aren't on that channel");
				return;
			}
			if (sign == '+') {
				if (channel.isOperator(*tgt) == false) {
					channel.addOperator(*tgt);
					record(changes, '+', 'o');
					changes.args.push_back(arg);
				}
			} else {
				if (channel.isOperator(*tgt) == true) {
					channel.removeOperator(*tgt);
					record(changes, '-', 'o');
					changes.args.push_back(arg);
				}
			}
			return;
		}
		default:
			return;
		}
	}

	void applyModes(Server& server, Client& client, Channel& channel,
			const std::string& modestr, const Message& msg, ModeChanges& changes) {
		std::size_t argIdx = 2;
		char sign = '+';
		std::size_t paramModeCount = 0;
		bool sentNeedMoreParams = false;
		for (std::string::size_type i = 0; i < modestr.size(); ++i) {
			ModeStep st = nextModeStep(modestr[i], sign, msg, argIdx);
			if (st.isSign == true)
				continue;
			if (st.spec == 0) {
				sendNumeric(server, client, Reply::ERR_UNKNOWNMODE,
						std::string(1, modestr[i])
						+ " :is unknown mode char to me for " + channel.name());
				continue;
			}
			if (st.takesArg == true) {
				if (paramModeCount >= MAX_PARAM_MODES)
					continue;
				if (st.argMissing == true) {
					if (sentNeedMoreParams == false) {
						sendNumeric(server, client, Reply::ERR_NEEDMOREPARAMS,
								"MODE :Not enough parameters");
						sentNeedMoreParams = true;
					}
					continue;
				}
				++paramModeCount;
			}
			if (st.takesArg == true && st.spec->validate != 0 && st.spec->validate(sign, st.arg) == false) {
				if (st.spec->badArgReply != 0)
					sendNumeric(server, client, st.spec->badArgReply,
							std::string("MODE ") + st.spec->badArgDetail);
				continue;
			}
			applyOne(server, client, channel, sign, modestr[i], st.arg, changes);
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
	if (msg.size() == 0 || msg.param(0).empty() == true) {
		throw IrcException(Reply::ERR_NEEDMOREPARAMS, client.nick(),
				"MODE :Not enough parameters");
	}
	const std::string& target = msg.param(0);
	if (target[0] != '#') {
		return;
	}
	Channel* channel = server.findChannel(target);
	if (channel == 0) {
		throw IrcException(Reply::ERR_NOSUCHCHANNEL, client.nick(),
				target + " :No such channel");
	}
	if (msg.size() < 2) {
		sendNumeric(server, client, Reply::RPL_CHANNELMODEIS,
				channel->name() + " " + channel->modeString(client));
		return;
	}
	if (msg.size() == 2 && (msg.param(1) == "b" || msg.param(1) == "+b")) {
		sendNumeric(server, client, Reply::RPL_ENDOFBANLIST,
				channel->name() + " :End of channel ban list");
		return;
	}
	if (channel->isOperator(client) == false) {
		throw IrcException(Reply::ERR_CHANOPRIVSNEEDED, client.nick(),
				channel->name() + " :You're not channel operator");
	}
	const std::string& modestr = msg.param(1);
	ModeChanges changes;
	applyModes(server, client, *channel, modestr, msg, changes);
	if (changes.any() == true) {
		const std::string applied = buildAppliedString(changes);
		channel->broadcast(Reply::from(client.prefix(),
				"MODE " + channel->name() + " " + applied));
		Log::mode("@ " + client.nick() + " set " + channel->name() + " "
				+ applied);
	}
}
