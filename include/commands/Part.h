#ifndef PART_COMMAND_H
#define PART_COMMAND_H

#include "../ACommand.h"

class PartCommand : public ACommand
{
public:
	void execute(Server& server, Client& client, const Message& msg);
	bool needsRegistration() const;
};

#endif
