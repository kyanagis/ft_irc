#include "Client.hpp"

Client::Client(int fd, const std::string& host)
		: _fd(fd),
			_host(host),
			_nick(),
			_user(),
			_realname(),
			_inBuf(),
			_outBuf(),
			_passOk(false),
			_hasNick(false),
			_hasUser(false),
			_registered(false),
			_channels() {
}

int Client::fd() const {
	return _fd;
}

const std::string& Client::host() const {
	return _host;
}

bool Client::passAccepted() const {
	return _passOk;
}

void Client::acceptPass() {
	_passOk = true;
}

bool Client::hasNick() const {
	return _hasNick;
}

const std::string& Client::nick() const {
	return _nick;
}

void Client::setNick(const std::string& nick) {
	_nick = nick;
	_hasNick = true;
}

bool Client::hasUser() const {
	return _hasUser;
}

const std::string& Client::user() const {
	return _user;
}

const std::string& Client::realname() const {
	return _realname;
}

void Client::setUser(const std::string& user, const std::string& realname) {
	_user = user;
	_realname = realname;
	_hasUser = true;
}

bool Client::isRegistered() const {
	return _registered;
}

void Client::markRegistered() {
	_registered = true;
}

std::string Client::prefix() const {
	std::string p;
	if (_nick.empty()) {
		p = "*";
	} else {
		p = _nick;
	}
	if (!_user.empty()) {
		p += "!" + _user;
	}
	if (!_host.empty()) {
		p += "@" + _host;
	}
	return p;
}

void Client::appendInput(const char* data, std::size_t n) {
	_inBuf.append(data, n);
}

bool Client::extractLine(std::string& out) {
	std::string::size_type pos = _inBuf.find('\n');
	if (pos == std::string::npos) {
		return false;
	}

	std::string line = _inBuf.substr(0, pos);
	if (!line.empty() && line[line.size() - 1] == '\r') {
		line.erase(line.size() - 1);
	}

	out = line;
	_inBuf.erase(0, pos + 1);
	return true;
}

void Client::appendOutput(const std::string& raw) {
	_outBuf += raw;
}

std::string& Client::outBuffer() {
	return _outBuf;
}

bool Client::hasPendingOutput() const {
	return !_outBuf.empty();
}

void Client::joinChannel(const std::string& name) {
	_channels.insert(name);
}

void Client::leaveChannel(const std::string& name) {
	_channels.erase(name);
}

const std::set<std::string>& Client::channels() const {
	return _channels;
}
