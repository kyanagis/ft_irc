#ifndef USER_COMMAND_HPP
#define USER_COMMAND_HPP

#include "../ACommand.hpp"

class UserCommand : public ACommand
{
public:
	void execute(Server& server, Client& client, const Message& msg);
	bool needsRegistration() const;
};

#endif
