#include "commands/Privmsg.hpp"

#include <set>

#include "Channel.hpp"
#include "Client.hpp"
#include "IrcException.hpp"
#include "Log.hpp"
#include "Message.hpp"
#include "Reply.hpp"
#include "Server.hpp"
#include "StringUtil.hpp"

namespace {

std::string bytes(const std::string& line) {
    return StringUtil::toString(static_cast<long>(line.size())) + "B";
}

void sendToChannel(Server& server, Client& client, const std::string& target,
                   const std::string& line) {
    Channel* channel = server.findChannel(target);
    if (channel == 0) {
        server.sendLine(
            client,
            Reply::numeric(server.serverName(), Reply::ERR_NOSUCHNICK,
                           client.nick(), target + " :No such nick/channel"));
        return;
    }
    if (!channel->hasMember(client)) {
        server.sendLine(
            client,
            Reply::numeric(server.serverName(), Reply::ERR_CANNOTSENDTOCHAN,
                           client.nick(), target + " :Cannot send to channel"));
        return;
    }
    channel->broadcast(line, &client);
    if (Log::traceEnabled()) {
        Log::relay("PRIVMSG " + client.nick() + " -> " + channel->name() + " " +
                   bytes(line) + " to " +
                   StringUtil::toString(
                       static_cast<long>(channel->memberCount() - 1)) +
                   " member(s)");
    }
}

void sendToUser(Server& server, Client& client, const std::string& target,
                const std::string& line) {
    Client* user = server.findClientByNick(target);
    if (user == 0) {
        server.sendLine(
            client,
            Reply::numeric(server.serverName(), Reply::ERR_NOSUCHNICK,
                           client.nick(), target + " :No such nick/channel"));
        return;
    }
    server.sendLine(*user, line);
    if (Log::traceEnabled()) {
        Log::relay("PRIVMSG " + client.nick() + " -> " + user->nick() + " " +
                   bytes(line));
    }
}

}

void PrivmsgCommand::execute(Server& server, Client& client,
                             const Message& msg) {
    if (msg.size() == 0 || msg.param(0).empty()) {
        throw IrcException(Reply::ERR_NORECIPIENT, client.nick(),
                           ":No recipient given (PRIVMSG)");
    }
    if (msg.size() < 2 || msg.param(1).empty()) {
        throw IrcException(Reply::ERR_NOTEXTTOSEND, client.nick(),
                           ":No text to send");
    }

    std::vector<std::string> targets = StringUtil::split(msg.param(0), ',');
    std::set<std::string> uniqueTargets;
    for (std::vector<std::string>::iterator it = targets.begin();
         it != targets.end(); ++it) {
        const std::string& target = *it;
        if (target.empty()) {
            continue;
        }
        const std::string foldedTarget = StringUtil::ircCaseFold(target);
        if (!uniqueTargets.insert(foldedTarget).second) {
            server.sendLine(
                client,
                Reply::numeric(
                    server.serverName(), Reply::ERR_TOOMANYTARGETS,
                    client.nick(),
                    target + " :Duplicate recipients. No message delivered"));
            continue;
        }

        std::string line = Reply::from(
            client.prefix(), "PRIVMSG " + target + " :" + msg.param(1));
        if (target[0] == '#') {
            sendToChannel(server, client, target, line);
        } else {
            sendToUser(server, client, target, line);
        }
    }
}

bool PrivmsgCommand::needsRegistration() const { return true; }
