#ifndef MODE_COMMAND_H
#define MODE_COMMAND_H

#include "../ACommand.h"

class ModeCommand : public ACommand
{
public:
	void execute(Server& server, Client& client, const Message& msg);
	bool needsRegistration() const;
};

#endif
