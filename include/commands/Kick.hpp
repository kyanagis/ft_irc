#ifndef KICK_COMMAND_HPP
#define KICK_COMMAND_HPP

#include "../ACommand.hpp"

class KickCommand : public ACommand
{
public:
	void execute(Server& server, Client& client, const Message& msg);
	bool needsRegistration() const;
};

#endif
