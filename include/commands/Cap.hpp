#ifndef CAP_COMMAND_H
#define CAP_COMMAND_H

#include "../ACommand.hpp"

class CapCommand : public ACommand
{
public:
	void execute(Server& server, Client& client, const Message& msg);
	bool needsRegistration() const;
};

#endif
