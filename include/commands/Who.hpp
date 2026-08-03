#ifndef WHO_COMMAND_HPP
#define WHO_COMMAND_HPP

#include "../ACommand.hpp"

class WhoCommand : public ACommand
{
public:
	void execute(Server& server, Client& client, const Message& msg);
	bool needsRegistration() const;
};

#endif
