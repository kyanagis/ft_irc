#ifndef PING_COMMAND_H
#define PING_COMMAND_H

#include "../ACommand.h"

class PingCommand : public ACommand
{
public:
	void execute(Server& server, Client& client, const Message& msg);
	bool needsRegistration() const;
};

#endif
