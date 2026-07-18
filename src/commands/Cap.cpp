#include "commands/Cap.hpp"

#include "Client.hpp"
#include "IrcException.hpp" 
#include "Message.hpp"
#include "Reply.hpp"
#include "Server.hpp"

bool CapCommand::needsRegistration() const {
    return false;
}

void CapCommand::execute(Server& server, Client& client, const Message& msg) {
    if (msg.size() == 0) {
        return;  
    }

    const std::string& subcommand = msg.param(0);
    if (subcommand == "LS") {
        server.sendLine(client, ":" + server.serverName() + " CAP * LS :");
    } else if (subcommand == "LIST"){
        server.sendLine(client, ":" + server.serverName() + " CAP * LIST :");
    }
    else if (subcommand == "REQ") {
        server.sendLine(client, ":" + server.serverName() + " CAP * NAK :" + msg.param(1));
    } else {
        return ;
    }
}

