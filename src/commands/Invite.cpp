#include "commands/Invite.hpp"

#include "Channel.hpp"
#include "Client.hpp"
#include "IrcException.hpp"
#include "Message.hpp"
#include "Reply.hpp"
#include "Server.hpp"

void InviteCommand::execute(Server& server, Client& client,
                            const Message& msg) {
    if (msg.size() < 2) {
        server.sendLine(
            client,
            Reply::numeric(server.serverName(), Reply::ERR_NEEDMOREPARAMS,
                           client.nick(), "INVITE :Not enough parameters"));
        return;
    }

    const std::string& targetNick = msg.param(0);
    const std::string& channelName = msg.param(1);
    Client* targetClient = server.findClientByNick(targetNick);
    if (targetClient == 0) {
        server.sendLine(
            client, Reply::numeric(server.serverName(), Reply::ERR_NOSUCHNICK,
                                   client.nick(),
                                   targetNick + " :No such nick/channel"));
        return;
    }
    Channel* channel = server.findChannel(channelName);
    if (channel == 0) {
        server.sendLine(
            client,
            Reply::numeric(server.serverName(), Reply::ERR_NOSUCHCHANNEL,
                           client.nick(), channelName + " :No such channel"));
        return;
    }
    if (!channel->hasMember(client)) {
        server.sendLine(
            client,
            Reply::numeric(server.serverName(), Reply::ERR_NOTONCHANNEL,
                           client.nick(),
                           channelName + " :You're not on that channel"));
        return;
    }
    if (channel->inviteOnly() && !channel->isOperator(client)) {
        server.sendLine(
            client,
            Reply::numeric(server.serverName(), Reply::ERR_CHANOPRIVSNEEDED,
                           client.nick(),
                           channelName + " :You're not channel operator"));
        return;
    }
    if (channel->hasMember(*targetClient)) {
        server.sendLine(
            client,
            Reply::numeric(
                server.serverName(), Reply::ERR_USERONCHANNEL, client.nick(),
                targetNick + " " + channelName + " :is already on channel"));
        return;
    }

    channel->invite(*targetClient);
    server.sendLine(
        client, Reply::numeric(server.serverName(), Reply::RPL_INVITING,
                               client.nick(), targetNick + " " + channelName));
    server.sendLine(*targetClient,
                    Reply::from(client.prefix(),
                                "INVITE " + targetNick + " :" + channelName));
}

bool InviteCommand::needsRegistration() const { return true; }
