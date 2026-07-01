#ifndef ACOMMAND_HPP
#define ACOMMAND_HPP

class Server;
class Client;
class Message;

class ACommand
{
public:
	virtual ~ACommand();

	virtual void execute(Server& server, Client& client, const Message& msg) = 0;

	virtual bool needsRegistration() const = 0;
};

#endif
