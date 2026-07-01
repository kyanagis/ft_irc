#ifndef PING_COMMAND_HPP
#define PING_COMMAND_HPP

#include "../ACommand.hpp"

class PingCommand : public ACommand
{
public:
	void execute(Server& server, Client& client, const Message& msg);
	bool needsRegistration() const;
};

#endif
