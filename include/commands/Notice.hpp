#ifndef NOTICE_COMMAND_H
#define NOTICE_COMMAND_H

#include "../ACommand.hpp"

class NoticeCommand : public ACommand
{
public:
	void execute(Server& server, Client& client, const Message& msg);
	bool needsRegistration() const;
};

#endif
