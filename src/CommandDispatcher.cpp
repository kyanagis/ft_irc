#include "CommandDispatcher.hpp"

#include <exception>

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
#include "Topic.hpp"
#include "Ping.hpp"
#include "Cap.hpp"
#include "Quit.hpp"

CommandDispatcher::CommandDispatcher() {
	// コマンド担当がここで登録する
	// 登録名は大文字（Message::parseがcommandを大文字化するため）
	registerCommand("PASS", new PassCommand());
	registerCommand("NICK", new NickCommand());
	registerCommand("USER", new UserCommand());
	registerCommand("JOIN", new JoinCommand());
	registerCommand("PART", new PartCommand());
	registerCommand("TOPIC", new TopicCommand());
	registerCommand("PING", new PingCommand());
	registerCommand("CAP", new CapCommand());
	registerCommand("QUIT", new QuitCommand());
}

CommandDispatcher::~CommandDispatcher() {
	for (std::map<std::string, ACommand*>::iterator it = _table.begin();
			it != _table.end(); ++it) {
		delete it->second;
	}
	_table.clear();
}

void CommandDispatcher::registerCommand(const std::string& name,
		ACommand* command) {
	_table[name] = command;
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
	catch (const std::exception&) {
	}
}
