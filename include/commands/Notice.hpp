#ifndef NOTICE_COMMAND_HPP
#define NOTICE_COMMAND_HPP

#include "../ACommand.hpp"

class NoticeCommand : public ACommand
{
public:
	void execute(Server& server, Client& client, const Message& msg);
	bool needsRegistration() const;
};

#endif
