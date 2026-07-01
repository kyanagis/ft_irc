#ifndef MODE_COMMAND_HPP
#define MODE_COMMAND_HPP

#include "../ACommand.hpp"

class ModeCommand : public ACommand
{
public:
	void execute(Server& server, Client& client, const Message& msg);
	bool needsRegistration() const;
};

#endif
