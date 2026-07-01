#include "Socket.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace
{
	std::string sysError(const std::string& op)
	{
		return "Socket: " + op + " failed: " + std::strerror(errno);
	}
}

Socket::Socket() : _fd(-1)
{
}

Socket::~Socket()
{
	if (_fd >= 0)
		close(_fd);
}

int Socket::fd() const
{
	return _fd;
}

void Socket::setNonBlocking(int fd)
{
	if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1)
		throw std::runtime_error(sysError("fcntl(O_NONBLOCK)"));
}

void Socket::openListen(int port)
{
	if (port < 1 || port > 65535)
		throw std::runtime_error("Socket: invalid port (1-65535)");

	int listenFd = socket(AF_INET, SOCK_STREAM, 0);
	if (listenFd == -1)
		throw std::runtime_error(sysError("socket"));

	int opt = 1;
	if (setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
	{
		close(listenFd);
		throw std::runtime_error(sysError("setsockopt(SO_REUSEADDR)"));
	}

	struct sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(static_cast<unsigned short>(port));

	if (bind(listenFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == -1)
	{
		close(listenFd);
		throw std::runtime_error(sysError("bind"));
	}

	if (listen(listenFd, SOMAXCONN) == -1)
	{
		close(listenFd);
		throw std::runtime_error(sysError("listen"));
	}

	try
	{
		setNonBlocking(listenFd);
	}
	catch (...)
	{
		close(listenFd);
		throw;
	}

	_fd = listenFd;
}

int Socket::acceptClient(std::string& hostOut)
{
	struct sockaddr_in clientAddr;
	socklen_t len = sizeof(clientAddr);

	int clientFd = accept(_fd, reinterpret_cast<struct sockaddr*>(&clientAddr), &len);
	if (clientFd == -1)
		return -1;

	try
	{
		setNonBlocking(clientFd);
	}
	catch (...)
	{
		close(clientFd);
		return -1;
	}

	char buf[INET_ADDRSTRLEN];
	if (inet_ntop(AF_INET, &clientAddr.sin_addr, buf, sizeof(buf)) != NULL)
		hostOut = buf;
	else
		hostOut = "unknown";

	return clientFd;
}
