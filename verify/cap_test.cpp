#include <iostream>
#include <cassert>

#include "../include/commands/Cap.hpp"
#include "../include/Client.hpp"
#include "../include/Message.hpp"
#include "../include/Server.hpp"

// compile
// g++ -Wall -Wextra -Iinclude -Iinclude/commands -o verify/cap_test verify/cap_test.cpp src/ACommand.cpp src/Client.cpp src/CommandDispatcher.cpp src/IrcException.cpp src/Message.cpp src/Reply.cpp src/Server.cpp src/Socket.cpp src/StringUtil.cpp src/Channel/*.cpp src/commands/*.cpp -std=c++98

int main() {

    Server server(6667, "pass");
    Client client(1, "localhost");

    CapCommand cmd;

    {
        Message msg = Message::parse("CAP LS");
        cmd.execute(server, client, msg);
        std::string out = client.outBuffer();
        if (out.find("CAP * LS") == std::string::npos) {
            std::cerr << "CAP LS failed: " << out << std::endl;
            return 2;
        }
    }

    {
        Message msg = Message::parse("CAP REQ some-ext");
        cmd.execute(server, client, msg);
        std::string out = client.outBuffer();
        if (out.find("CAP * NAK :some-ext") == std::string::npos) {
            std::cerr << "CAP REQ failed: " << out << std::endl;
            return 3;
        }
    }

    Message msg = Message::parse("CAP UNKNOWN");
    cmd.execute(server, client, msg);

    std::cout << "OK" << std::endl;
    return 0;
}
