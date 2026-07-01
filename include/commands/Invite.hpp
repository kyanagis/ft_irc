#ifndef INVITE_COMMAND_HPP
#define INVITE_COMMAND_HPP

#include "../ACommand.hpp"

class InviteCommand : public ACommand
{
public:
	void execute(Server& server, Client& client, const Message& msg);
	bool needsRegistration() const;
};

#endif
