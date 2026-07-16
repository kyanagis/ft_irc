#include "commands/Notice.hpp"

#include <set>

#include "Channel.hpp"
#include "Client.hpp"
#include "IrcException.hpp"
#include "Message.hpp"
#include "Reply.hpp"
#include "Server.hpp"
#include "StringUtil.hpp"

namespace {

void sendToChannel(Server& server, Client& client, const std::string& target,
                   const std::string& line) {
    Channel* channel = server.findChannel(target);
    if (channel == 0) {
        return;
    }
    if (!channel->hasMember(client)) {
        return;
    }
    channel->broadcast(line, &client);
}

void sendToUser(Server& server, const std::string& target,
                const std::string& line) {
    Client* user = server.findClientByNick(target);
    if (user == 0) {
        return;
    }
    server.sendLine(*user, line);
}

}  // namespace

void NoticeCommand::execute(Server& server, Client& client,
                            const Message& msg) {
    if (msg.size() == 0 || msg.param(0).empty()) {
        return;
    }
    if (msg.size() < 2 || msg.param(1).empty()) {
        return;
    }

    std::vector<std::string> targets = StringUtil::split(msg.param(0), ',');
    std::set<std::string> uniqueTargets;
    for (std::vector<std::string>::iterator it = targets.begin();
         it != targets.end(); ++it) {
        const std::string& target = *it;
        if (target.empty()) {
            continue;
        }

        const std::string upperTarget = StringUtil::toUpper(target);
        if (!uniqueTargets.insert(upperTarget).second) {
            continue;
        }

        std::string line = Reply::from(
            client.prefix(), "NOTICE " + target + " :" + msg.param(1));

        if (target[0] == '#') {
            sendToChannel(server, client, target, line);
        } else {
            sendToUser(server, target, line);
        }
    }
}

bool NoticeCommand::needsRegistration() const { return true; }
