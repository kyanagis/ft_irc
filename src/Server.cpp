#include "Server.hpp"
#include "Client.hpp"
#include "Channel.hpp"

#include <sys/socket.h>
#include <unistd.h>
#include <csignal>
#include <cerrno>
#include <cstring>
#include <cstddef>
#include <stdexcept>

namespace
{
	const std::size_t READ_CHUNK = 4096;
}

volatile sig_atomic_t Server::_running = 0;

Server::Server(int port, const std::string& password)
	: _listen(),
	  _port(port),
	  _password(password),
	  _serverName("ircserv"),
	  _createdAt("(startup)"),
	  _clients(),
	  _channels(),
	  _pollfds(),
	  _dispatcher()
{
}

Server::~Server()
{
	for (std::map<int, Client*>::iterator it = _clients.begin(); it != _clients.end(); ++it)
	{
		close(it->first);
		delete it->second;
	}
	_clients.clear();

	for (std::map<std::string, Channel*>::iterator it = _channels.begin(); it != _channels.end(); ++it)
		delete it->second;
	_channels.clear();
}

const std::string& Server::password() const   { return _password; }
const std::string& Server::serverName() const  { return _serverName; }
const std::string& Server::createdAt() const   { return _createdAt; }

void Server::requestStop(int signum)
{
	(void)signum;
	_running = 0;
}

void Server::setup()
{
	_listen.openListen(_port);
}

void Server::rebuildPollFds()
{
	_pollfds.clear();

	struct pollfd listenPfd;
	listenPfd.fd = _listen.fd();
	listenPfd.events = POLLIN;
	listenPfd.revents = 0;
	_pollfds.push_back(listenPfd);

	for (std::map<int, Client*>::iterator it = _clients.begin(); it != _clients.end(); ++it)
	{
		struct pollfd pfd;
		pfd.fd = it->first;
		pfd.events = POLLIN;
		if (it->second->hasPendingOutput())
			pfd.events |= POLLOUT;
		pfd.revents = 0;
		_pollfds.push_back(pfd);
	}
}

void Server::run()
{
	setup();
	_running = 1;

	std::signal(SIGINT, Server::requestStop);
	std::signal(SIGTERM, Server::requestStop);
	std::signal(SIGPIPE, SIG_IGN);

	while (_running)
	{
		rebuildPollFds();

		int ready = poll(&_pollfds[0], static_cast<nfds_t>(_pollfds.size()), -1);
		if (ready < 0)
		{
			if (errno == EINTR)
				continue;
			throw std::runtime_error(std::string("poll: ") + std::strerror(errno));
		}

		for (std::size_t i = 0; i < _pollfds.size(); ++i)
		{
			short re = _pollfds[i].revents;
			if (re == 0)
				continue;

			int fd = _pollfds[i].fd;

			if (fd == _listen.fd())
			{
				if (re & POLLIN)
					acceptClient();
				continue;
			}

			std::map<int, Client*>::iterator it = _clients.find(fd);
			if (it == _clients.end())
				continue;
			Client* client = it->second;

			if (re & POLLIN)
			{
				handleReadable(*client);
				if (_clients.find(fd) == _clients.end())
					continue;
			}
			if (re & POLLOUT)
			{
				handleWritable(*client);
				if (_clients.find(fd) == _clients.end())
					continue;
			}
			if (re & (POLLERR | POLLHUP | POLLNVAL))
				disconnect(*client, "poll error/hangup");
		}
	}
}

void Server::acceptClient()
{
	std::string host;
	int fd;
	while ((fd = _listen.acceptClient(host)) >= 0)
	{
		Client* client = 0;
		try
		{
			client = new Client(fd, host);
		}
		catch (...)
		{
			close(fd);
			return;
		}
		_clients[fd] = client;
	}
}

void Server::handleReadable(Client& client)
{
	char buf[READ_CHUNK];
	ssize_t n;

	while ((n = recv(client.fd(), buf, sizeof(buf), 0)) > 0)
		client.appendInput(buf, static_cast<std::size_t>(n));

	if (n == 0)
	{
		disconnect(client, "client closed connection");
		return;
	}
	pumpLines(client);
}

void Server::handleWritable(Client& client)
{
	std::string& out = client.outBuffer();
	if (out.empty())
		return;

	ssize_t n = send(client.fd(), out.c_str(), out.size(), 0);
	if (n > 0)
		out.erase(0, static_cast<std::size_t>(n));
}

void Server::pumpLines(Client& client)
{
	std::string line;
	while (client.extractLine(line))
		queueMessage(client, line + "\r\n");
}

void Server::queueMessage(Client& client, const std::string& message)
{
	client.appendOutput(message);
}

void Server::disconnect(Client& client, const std::string& reason)
{
	(void)reason;
	int fd = client.fd();

	_clients.erase(fd);
	close(fd);
	delete &client;
}
