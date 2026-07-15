#include "commands/Quit.hpp"

#include "Client.hpp"
#include "Message.hpp"
#include "Reply.hpp"
#include "Server.hpp"

void QuitCommand::execute(Server& server, Client& client, const Message& msg) {
    std::string reason = "Client Quit";
    if (msg.size() >= 1)
        reason = msg.param(0);

    server.disconnect(client, reason);
}

bool QuitCommand::needsRegistration() const {
    return false;
}
