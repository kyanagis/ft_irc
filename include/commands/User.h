#ifndef USER_COMMAND_H
#define USER_COMMAND_H

#include "../ACommand.h"

class UserCommand : public ACommand
{
public:
	void execute(Server& server, Client& client, const Message& msg);
	bool needsRegistration() const;
};

#endif
