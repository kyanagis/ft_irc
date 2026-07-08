#include "Channel.hpp"
#include "Client.hpp"

void Channel::broadcast(const std::string& message, Client* except) {
	// 呼び出し側はCRLF無しの1行を渡す規約。ここで行終端を付与する。
	// 連結はループ前に1回だけ行い、各メンバへは同じ文字列を使い回す。
	const std::string line = message + "\r\n";
	for (std::set<Client*>::const_iterator it = _members.begin();
	     it != _members.end(); ++it) {
		if (*it == except)
			continue;
		(*it)->appendOutput(line);
	}
}
