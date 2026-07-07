#include "CommandDispatcher.hpp"

#include <exception>

#include "ACommand.hpp"
#include "Client.hpp"
#include "IrcException.hpp"
#include "Message.hpp"
#include "Reply.hpp"
#include "Server.hpp"

namespace {
	// IRCの1行はCRLF終端。queueMessageは生追記なのでここで付ける
	void send(Server& server, Client& client, const std::string& line) {
		server.queueMessage(client, line + "\r\n");
	}
}

CommandDispatcher::CommandDispatcher() {
	// registerCommand("PASS", new PassCommand()) のようにコマンド担当がここで登録する
	// 登録名は大文字（Message::parseがcommandを大文字化するため）
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
		send(server, client, Reply::numeric(server.serverName(),
				Reply::ERR_UNKNOWNCOMMAND, client.nick(),
				msg.command() + " :Unknown command"));
		return;
	}

	ACommand* command = it->second;
	if (command->needsRegistration() && !client.isRegistered()) {
		send(server, client, Reply::numeric(server.serverName(),
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
		send(server, client, Reply::numeric(server.serverName(), e.code(),
				e.target(), e.detail()));
	}
	catch (const std::exception&) {
	}
}
