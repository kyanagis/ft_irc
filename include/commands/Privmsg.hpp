#ifndef PRIVMSG_COMMAND_H
#define PRIVMSG_COMMAND_H

#include "../ACommand.hpp"

class PrivmsgCommand : public ACommand
{
public:
	void execute(Server& server, Client& client, const Message& msg);
	bool needsRegistration() const;
};

#endif
