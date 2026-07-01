#ifndef JOIN_COMMAND_H
#define JOIN_COMMAND_H

#include "../ACommand.hpp"

class JoinCommand : public ACommand
{
public:
	void execute(Server& server, Client& client, const Message& msg);
	bool needsRegistration() const;
};

#endif
