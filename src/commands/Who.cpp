#include "commands/Who.hpp"

#include <set>
#include <string>

#include "Channel.hpp"
#include "Client.hpp"
#include "Message.hpp"
#include "Reply.hpp"
#include "Server.hpp"

// WHO は subject の必須機能ではないが，irssi が JOIN 直後に自動送出するため
// 未実装だと 421 が返り，クライアント側にエラーとして出る．
// RFC 2812 §3.6.1 の最小形（352 の羅列＋315 で終端）だけを返す．
// マスク展開（'*' やワイルドカード）や不可視ユーザの扱いは実装しない．
namespace {
	void sendNumeric(Server& server, Client& client, int code,
			const std::string& detail) {
		server.sendLine(client, Reply::numeric(server.serverName(), code,
				client.nick(), detail));
	}

	// 352: <channel> <user> <host> <server> <nick> <H|G>[@] :<hopcount> <realname>
	// H=here 固定（AWAY未実装）．channel が無い相手は RFC 通り "*" を入れる
	void sendWhoReply(Server& server, Client& client,
			const std::string& channelName, const Client& target, bool isOp) {
		std::string detail = channelName
				+ " " + target.user()
				+ " " + target.host()
				+ " " + server.serverName()
				+ " " + target.nick()
				+ " H";
		if (isOp) {
			detail += "@";
		}
		detail += " :0 " + target.realname();
		sendNumeric(server, client, Reply::RPL_WHOREPLY, detail);
	}
}

bool WhoCommand::needsRegistration() const {
	return true;
}

void WhoCommand::execute(Server& server, Client& client, const Message& msg) {
	const std::string& mask = msg.param(0);

	if (!mask.empty() && mask[0] == '#') {
		Channel* channel = server.findChannel(mask);
		if (channel != 0) {
			const std::set<Client*>& members = channel->members();
			for (std::set<Client*>::const_iterator it = members.begin();
					it != members.end(); ++it) {
				sendWhoReply(server, client, channel->name(), **it,
						channel->isOperator(**it));
			}
		}
	} else if (!mask.empty()) {
		Client* target = server.findClientByNick(mask);
		// 未登録（USER未送信）だと user/realname が空で 352 が壊れるため除く
		if (target != 0 && target->isRegistered()) {
			sendWhoReply(server, client, "*", *target, false);
		}
	}

	// 一致が無くても終端は必ず返す（クライアントが待ち続けないように）
	sendNumeric(server, client, Reply::RPL_ENDOFWHO,
			(mask.empty() ? std::string("*") : mask) + " :End of WHO list");
}
