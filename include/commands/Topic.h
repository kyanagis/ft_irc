#ifndef TOPIC_COMMAND_H
#define TOPIC_COMMAND_H

#include "../ACommand.h"

class TopicCommand : public ACommand
{
public:
	void execute(Server& server, Client& client, const Message& msg);
	bool needsRegistration() const;
};

#endif
