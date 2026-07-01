#ifndef SERVER_HPP
#define SERVER_HPP

#include <map>
#include <vector>
#include <string>
#include <csignal>
#include <poll.h>

#include "Socket.hpp"
#include "CommandDispatcher.hpp"

class Client;
class Channel;

class Server
{
public:
	Server(int port, const std::string& password);
	~Server();

	void run();

	Client*  findClientByNick(const std::string& nick);
	Channel* findChannel(const std::string& name);
	Channel* getOrCreateChannel(const std::string& name, Client& creator);
	void     removeEmptyChannel(Channel* channel);
	void     disconnect(Client& client, const std::string& reason);
	void     queueMessage(Client& client, const std::string& message);

	const std::string& password() const;
	const std::string& serverName() const;
	const std::string& createdAt() const;

	static void requestStop(int signum);

private:
	Server(const Server&);
	Server& operator=(const Server&);

	void setup();
	void acceptClient();
	void handleReadable(Client& client);
	void handleWritable(Client& client);
	void pumpLines(Client& client);
	void rebuildPollFds();

	Socket                          _listen;
	int                             _port;
	std::string                     _password;
	std::string                     _serverName;
	std::string                     _createdAt;
	std::map<int, Client*>          _clients;
	std::map<std::string, Channel*> _channels;
	std::vector<struct pollfd>      _pollfds;
	CommandDispatcher               _dispatcher;

	static volatile sig_atomic_t    _running;
};

#endif
