#ifndef PASS_COMMAND_H
#define PASS_COMMAND_H

#include "../ACommand.hpp"

class PassCommand : public ACommand
{
public:
	void execute(Server& server, Client& client, const Message& msg);
	bool needsRegistration() const;
};

#endif
