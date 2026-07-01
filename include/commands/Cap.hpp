#ifndef CAP_COMMAND_HPP
#define CAP_COMMAND_HPP

#include "../ACommand.hpp"

class CapCommand : public ACommand
{
public:
	void execute(Server& server, Client& client, const Message& msg);
	bool needsRegistration() const;
};

#endif
