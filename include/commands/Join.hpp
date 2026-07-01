#ifndef JOIN_COMMAND_HPP
#define JOIN_COMMAND_HPP

#include "../ACommand.hpp"

class JoinCommand : public ACommand
{
public:
	void execute(Server& server, Client& client, const Message& msg);
	bool needsRegistration() const;
};

#endif
