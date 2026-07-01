#ifndef PASS_COMMAND_HPP
#define PASS_COMMAND_HPP

#include "../ACommand.hpp"

class PassCommand : public ACommand
{
public:
	void execute(Server& server, Client& client, const Message& msg);
	bool needsRegistration() const;
};

#endif
