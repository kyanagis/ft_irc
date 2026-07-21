#include "commands/Ping.hpp"

#include "Client.hpp"
#include "IrcException.hpp"
#include "Message.hpp"
#include "Reply.hpp"
#include "Server.hpp"

void PingCommand::execute(Server& server, Client& client, const Message& msg) {
    if (msg.size() == 0 || msg.param(0).empty()) {
        throw IrcException(Reply::ERR_NOORIGIN, client.nick(), "No origin specified");
    }
    std::string response = "PONG " + server.serverName() + " :" + msg.param(0);
    server.sendLine(client, Reply::from(server.serverName(), response));
}

bool PingCommand::needsRegistration() const {
    return true;
}
