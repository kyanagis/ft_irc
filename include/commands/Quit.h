#ifndef QUIT_COMMAND_H
#define QUIT_COMMAND_H

#include "../ACommand.h"

class QuitCommand : public ACommand
{
public:
	void execute(Server& server, Client& client, const Message& msg);
	bool needsRegistration() const;
};

#endif
