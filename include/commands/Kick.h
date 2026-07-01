#ifndef KICK_COMMAND_H
#define KICK_COMMAND_H

#include "../ACommand.h"

class KickCommand : public ACommand
{
public:
	void execute(Server& server, Client& client, const Message& msg);
	bool needsRegistration() const;
};

#endif
