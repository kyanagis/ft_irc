#include "commands/Ping.hpp"

#include "Client.hpp"
#include "IrcException.hpp"
#include "Message.hpp"
#include "Reply.hpp"
#include "Server.hpp"

void PingCommand::execute(Server& server, Client& client, const Message& msg) {
    std::string response = "PONG " + server.serverName();
    if (msg.size() > 0 && !msg.param(0).empty()) {
        response += " :" + msg.param(0);
    }
    server.sendLine(client, Reply::from(server.serverName(), response));
}

bool PingCommand::needsRegistration() const {
    return true;
}
