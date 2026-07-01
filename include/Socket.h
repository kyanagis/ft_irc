#ifndef SOCKET_H
#define SOCKET_H

#include <string>

class Socket
{
public:
	Socket();
	~Socket();

	void openListen(int port);

	int acceptClient(std::string& hostOut);

	int fd() const;

private:
	Socket(const Socket&);
	Socket& operator=(const Socket&);

	static void setNonBlocking(int fd);

	int _fd;
};

#endif
