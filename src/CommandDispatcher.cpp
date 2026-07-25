#include "CommandDispatcher.hpp"

#include <exception>
#include <new>

#include "ACommand.hpp"
#include "Client.hpp"
#include "IrcException.hpp"
#include "Message.hpp"
#include "Reply.hpp"
#include "Server.hpp"
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

CommandDispatcher::CommandDispatcher() {
	try {
		// コマンド担当がここで登録する
		// 登録名は大文字（Message::parseがcommandを大文字化するため）
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
	}
	catch (...) {
		// 構築途中はデストラクタが呼ばれないため、登録済み分を明示解放する。
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
	// nameのstd::string化やmapノード確保が失敗しても、新規commandを解放する。
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
	std::map<std::string, ACommand*>::iterator it = _table.find(msg.command());
	if (it == _table.end()) {
		server.sendLine(client, Reply::numeric(server.serverName(),
				Reply::ERR_UNKNOWNCOMMAND, client.nick(),
				msg.command() + " :Unknown command"));
		return;
	}

	ACommand* command = it->second;
	if (command->needsRegistration() && !client.isRegistered()) {
		server.sendLine(client, Reply::numeric(server.serverName(),
				Reply::ERR_NOTREGISTERED, client.nick(),
				":You have not registered"));
		return;
	}

	// コマンドは検証失敗をIrcExceptionで投げる。ここで数値応答へ整形して返す。
	// 想定外の例外もサーバは落とさない（要件N8）。
	try {
		command->execute(server, client, msg);
	}
	catch (const IrcException& e) {
		server.sendLine(client, Reply::numeric(server.serverName(), e.code(),
				e.target(), e.detail()));
	}
	catch (const std::bad_alloc&) {
		throw;
	}
	// NOLINTNEXTLINE(bugprone-empty-catch): 想定外の例外でもサーバを落とさない（要件N8）。意図的に握り潰す。
	catch (const std::exception&) {
	}
}
