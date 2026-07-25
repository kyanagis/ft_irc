#include "CommandDispatcher.hpp"

#include <exception>

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

namespace {
	// IRC_TRACE=1 用の1行表記．PASS の引数はログに残さない
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
	// 引数の組み立て自体を避けるため呼び出し側で閉じる（既定オフ）。ここは dispatch の
	// try の外なので、確保を無条件に走らせると OOM 時に run() を抜けてしまう
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

	// コマンドは検証失敗をIrcExceptionで投げる。ここで数値応答へ整形して返す。
	// 想定外の例外もサーバは落とさない（要件N8）。
	try {
		command->execute(server, client, msg);
	}
	catch (const IrcException& e) {
		server.sendLine(client, Reply::numeric(server.serverName(), e.code(),
				e.target(), e.detail()));
		Log::deny(Log::who(client) + " " + msg.command() + " -> "
				+ StringUtil::toString(e.code()) + " " + e.detail());
	}
	// 想定外の例外でもサーバは落とさない（要件N8）。応答は返さずログだけ残す。
	// bad_alloc がここに来る場合があるので、確保しない oomWarn を使う
	catch (const std::exception& e) {
		Log::oomWarn("unexpected exception while handling",
				msg.command().c_str(), e.what());
	}
}
