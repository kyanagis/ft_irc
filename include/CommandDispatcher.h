#ifndef COMMAND_DISPATCHER_H
#define COMMAND_DISPATCHER_H

#include <map>
#include <string>

class ACommand;
class Server;
class Client;
class Message;

class CommandDispatcher
{
public:
	CommandDispatcher();
	~CommandDispatcher();

	void dispatch(Server& server, Client& client, const Message& msg);

private:
	CommandDispatcher(const CommandDispatcher&);
	CommandDispatcher& operator=(const CommandDispatcher&);

	void registerCommand(const std::string& name, ACommand* command);

	std::map<std::string, ACommand*> _table;
};

#endif
