#include "commands/Invite.hpp"

#include "Channel.hpp"
#include "Client.hpp"
#include "IrcException.hpp"
#include "Message.hpp"
#include "Reply.hpp"
#include "Server.hpp"

void InviteCommand::execute(Server& server, Client& client,
                            const Message& msg) {
    if (msg.size() < 2 || msg.param(0).empty() || msg.param(1).empty()) {
        throw IrcException(Reply::ERR_NEEDMOREPARAMS, client.nick() + " INVITE",
                           ":Not enough parameters");
    }

    const std::string& targetNick = msg.param(0);
    const std::string& channelName = msg.param(1);
    Client* targetClient = server.findClientByNick(targetNick);
    if (targetClient == 0) {
        throw IrcException(Reply::ERR_NOSUCHNICK, client.nick(),
                           targetNick + " :No such nick/channel");
    }
    Channel* channel = server.findChannel(channelName);
    if (channel == 0) {
        throw IrcException(Reply::ERR_NOSUCHCHANNEL, client.nick(),
                           channelName + " :No such channel");
    }
    if (!channel->hasMember(client)) {
        throw IrcException(Reply::ERR_NOTONCHANNEL, client.nick(),
                           channelName + " :You're not on that channel");
    }
    if (channel->inviteOnly() && !channel->isOperator(client)) {
        throw IrcException(Reply::ERR_CHANOPRIVSNEEDED, client.nick(),
                           channelName + " :You're not channel operator");
    }
    if (channel->hasMember(*targetClient)) {
        throw IrcException(
            Reply::ERR_USERONCHANNEL, client.nick(),
            targetNick + " " + channelName + " :is already on channel");
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
