#ifndef PART_COMMAND_HPP
#define PART_COMMAND_HPP

#include "../ACommand.hpp"

class PartCommand : public ACommand
{
public:
	void execute(Server& server, Client& client, const Message& msg);
	bool needsRegistration() const;
};

#endif
