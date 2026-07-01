#ifndef NICK_COMMAND_HPP
#define NICK_COMMAND_HPP

#include "../ACommand.hpp"

class NickCommand : public ACommand
{
public:
	void execute(Server& server, Client& client, const Message& msg);
	bool needsRegistration() const;
};

#endif
