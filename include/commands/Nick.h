#ifndef NICK_COMMAND_H
#define NICK_COMMAND_H

#include "../ACommand.h"

class NickCommand : public ACommand
{
public:
	void execute(Server& server, Client& client, const Message& msg);
	bool needsRegistration() const;
};

#endif
