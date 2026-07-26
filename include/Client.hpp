#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <string>
#include <set>
#include <cstddef>
#include <ctime>

class Client
{
public:
	Client(int fd, const std::string& host);

	int                fd() const;
	const std::string& host() const;

	bool passAccepted() const;
	void acceptPass();

	bool               hasNick() const;
	const std::string& nick() const;
	void               setNick(const std::string& nick);

	bool               hasUser() const;
	const std::string& user() const;
	const std::string& realname() const;
	void               setUser(const std::string& user, const std::string& realname);

	bool isRegistered() const;
	void markRegistered();

	std::string prefix() const;

	void         appendInput(const char* data, std::size_t n);
	bool         extractLine(std::string& out);
	bool         inputProtocolError() const;
	void         appendOutput(const std::string& raw);
	std::string& outBuffer();
	bool         hasPendingOutput() const;
	bool         inputOverflow() const;
	bool         outputOverflow() const;
	void         markReadClosed();
	bool         isReadClosed() const;
	std::time_t  connectedAt() const;
	std::time_t  closingSince() const;

	void                         joinChannel(const std::string& name);
	void                         leaveChannel(const std::string& name);
	const std::set<std::string>& channels() const;

private:
	Client(const Client&);
	Client& operator=(const Client&);

	int                   _fd;
	std::string           _host;
	std::string           _nick;
	std::string           _user;
	std::string           _realname;
	std::string           _inBuf;
	std::string           _outBuf;
	bool                  _passOk;
	bool                  _hasNick;
	bool                  _hasUser;
	bool                  _registered;
	bool                  _readClosed;
	bool                  _inputProtocolError;
	bool                  _outputOverflow;
	std::set<std::string> _channels;
	std::time_t           _connectedAt;
	std::time_t           _closingSince;
};

#endif
