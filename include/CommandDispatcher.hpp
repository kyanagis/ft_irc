#ifndef COMMAND_DISPATCHER_HPP
#define COMMAND_DISPATCHER_HPP

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

	void registerCommand(const char* name, ACommand* command);
	void clearCommands();

	std::map<std::string, ACommand*> _table;
};

#endif
