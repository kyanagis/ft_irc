#ifndef QUIT_COMMAND_HPP
#define QUIT_COMMAND_HPP

#include "../ACommand.hpp"

class QuitCommand : public ACommand
{
public:
	void execute(Server& server, Client& client, const Message& msg);
	bool needsRegistration() const;
};

#endif
