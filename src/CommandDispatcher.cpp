#include "CommandDispatcher.hpp"

#include <exception>
#include <new>

#include "ACommand.hpp"
#include "Client.hpp"
#include "IrcException.hpp"
#include "Log.hpp"
#include "Message.hpp"
#include "Reply.hpp"
#include "Server.hpp"
#include "StringUtil.hpp"
#include "Join.hpp"
#include "Pass.hpp"
#include "Nick.hpp"
#include "User.hpp"
#include "Part.hpp"
#include "Kick.hpp"
#include "Topic.hpp"
#include "Mode.hpp"
#include "Ping.hpp"
#include "Cap.hpp"
#include "Quit.hpp"
#include "Privmsg.hpp"
#include "Notice.hpp"
#include "Invite.hpp"
#include "Who.hpp"

namespace {
	std::string traceLine(const Message& msg) {
		std::string s = msg.command();
		if (msg.command() == "PASS") {
			return s + " ***";
		}
		for (std::size_t i = 0; i < msg.size(); ++i) {
			s += " " + msg.param(i);
		}
		return s;
	}
}

CommandDispatcher::CommandDispatcher() {
	try {
		registerCommand("PASS", new PassCommand());
		registerCommand("NICK", new NickCommand());
		registerCommand("USER", new UserCommand());
		registerCommand("JOIN", new JoinCommand());
		registerCommand("PART", new PartCommand());
		registerCommand("KICK", new KickCommand());
		registerCommand("TOPIC", new TopicCommand());
		registerCommand("MODE", new ModeCommand());
		registerCommand("PING", new PingCommand());
		registerCommand("CAP", new CapCommand());
		registerCommand("QUIT", new QuitCommand());
		registerCommand("NOTICE", new NoticeCommand());
		registerCommand("PRIVMSG", new PrivmsgCommand());
		registerCommand("INVITE", new InviteCommand());
		registerCommand("WHO", new WhoCommand());
	}
	catch (...) {
		clearCommands();
		throw;
	}
}

CommandDispatcher::~CommandDispatcher() {
	clearCommands();
}

void CommandDispatcher::clearCommands() {
	for (std::map<std::string, ACommand*>::iterator it = _table.begin();
			it != _table.end(); ++it) {
		delete it->second;
	}
	_table.clear();
}

void CommandDispatcher::registerCommand(const char* name,
		ACommand* command) {
	try {
		std::pair<std::map<std::string, ACommand*>::iterator, bool> inserted =
				_table.insert(std::make_pair(std::string(name), command));
		if (!inserted.second) {
			delete command;
		}
	}
	catch (...) {
		delete command;
		throw;
	}
}

void CommandDispatcher::dispatch(Server& server, Client& client,
		const Message& msg) {
	if (Log::traceEnabled()) {
		Log::trace(Log::who(client) + " > " + traceLine(msg));
	}

	std::map<std::string, ACommand*>::iterator it = _table.find(msg.command());
	if (it == _table.end()) {
		server.sendLine(client, Reply::numeric(server.serverName(),
				Reply::ERR_UNKNOWNCOMMAND, client.nick(),
				msg.command() + " :Unknown command"));
		Log::deny(Log::who(client) + " " + msg.command()
				+ " -> 421 unknown command");
		return;
	}

	ACommand* command = it->second;
	if (command->needsRegistration() && !client.isRegistered()) {
		server.sendLine(client, Reply::numeric(server.serverName(),
				Reply::ERR_NOTREGISTERED, client.nick(),
				":You have not registered"));
		Log::deny(Log::who(client) + " " + msg.command()
				+ " -> 451 not registered");
		return;
	}

	try {
		command->execute(server, client, msg);
	}
	catch (const IrcException& e) {
		server.sendLine(client, Reply::numeric(server.serverName(), e.code(),
				e.target(), e.detail()));
		Log::deny(Log::who(client) + " " + msg.command() + " -> "
				+ StringUtil::toString(e.code()) + " " + e.detail());
	}
	catch (const std::bad_alloc&) {
		throw;
	}
	catch (const std::exception& e) {
		Log::oomWarn("unexpected exception while handling",
				msg.command().c_str(), e.what());
	}
}
