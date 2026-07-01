#include "CommandDispatcher.hpp"

CommandDispatcher::CommandDispatcher()
{
}

CommandDispatcher::~CommandDispatcher()
{
}

void CommandDispatcher::dispatch(Server& server, Client& client, const Message& msg)
{
	(void)server;
	(void)client;
	(void)msg;
}
