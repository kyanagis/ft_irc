#include "commands/Privmsg.hpp"

#include <set>
#include <sstream>

#include "Channel.hpp"
#include "Client.hpp"
#include "IrcException.hpp"
#include "Message.hpp"
#include "Reply.hpp"
#include "Server.hpp"

namespace {

std::vector<std::string> split(const std::string& str, char delim) {
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delim)) {
        result.push_back(token);
    }
    return result;
}

void sendToChannel(Server& server, Client& client, const std::string& target,
                   const std::string& line) {
    Channel* channel = server.findChannel(target);
    if (channel == 0) {
        server.sendLine(
            client,
            Reply::numeric(server.serverName(), Reply::ERR_NOSUCHCHANNEL,
                           client.nick(), target + " :No such channel"));
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
    if (user == &client) {
        return;
    }
    server.sendLine(*user, line);
}

}  // namespace

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

    std::vector<std::string> targets = split(msg.param(0), ',');
    std::set<std::string> uniqueTargets;
    const std::string& text = msg.param(1);
    for (std::vector<std::string>::iterator it = targets.begin();
         it != targets.end(); ++it) {
        const std::string& target = *it;
        if (!uniqueTargets.insert(target).second) {
            server.sendLine(
                client,
                Reply::numeric(
                    server.serverName(), Reply::ERR_TOOMANYTARGETS,
                    client.nick(),
                    target + " :Duplicate recipients. No message delivered"));
            continue;
        }

        std::string line =
            Reply::from(client.prefix(), "PRIVMSG " + target + " :" + text);
        if (!target.empty() && target[0] == '#') {
            sendToChannel(server, client, target, line);
        } else {
            sendToUser(server, client, target, line);
        }
    }
}

bool PrivmsgCommand::needsRegistration() const { return true; }
