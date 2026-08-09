#include "Server.hpp"

#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstring>
#include <ctime>

#include <new>
#include <set>
#include <stdexcept>

#include <sys/socket.h>
#include <unistd.h>

#include "Channel.hpp"
#include "Client.hpp"
#include "Log.hpp"
#include "Message.hpp"
#include "Reply.hpp"
#include "StringUtil.hpp"

namespace {
	const std::size_t READ_CHUNK = 4096;
	const std::string IRC_CRLF = "\r\n";
	const std::string SERVER_VERSION = "1.0";
	const int POLL_TIMEOUT_MS = 1000;
	const std::time_t REG_TIMEOUT_SEC = 60;
	const std::time_t CLOSE_TIMEOUT_SEC = 10;
	const std::time_t LOG_DRAIN_SEC = 2;

	std::string formatDuration(std::time_t sec) {
		if (sec < 0) {
			sec = 0;
		}
		long total = static_cast<long>(sec);
		std::string r;
		if (total >= 3600) {
			r += StringUtil::toString(total / 3600) + "h";
			total %= 3600;
		}
		if (!r.empty() || total >= 60) {
			r += StringUtil::toString(total / 60) + "m";
			total %= 60;
		}
		return r + StringUtil::toString(total) + "s";
	}

	std::string buildErrorLine(const std::string& host, const std::string& reason) {
		std::string safe;
		safe.reserve(reason.size());
		for (std::string::size_type i = 0; i < reason.size(); ++i) {
			char c = reason[i];
			if (c != '\r' && c != '\n' && c != '\0') {
				safe += c;
			}
		}
		std::string line = "ERROR :Closing Link: " + host + " (" + safe + ")";
		if (line.size() > 510) {
			line.erase(510);
		}
		line += IRC_CRLF;
		return line;
	}
}

volatile sig_atomic_t Server::_running = 0;

Server::Server(int port, const std::string& password)
		: _listen(),
			_port(port),
			_password(password),
			_serverName("ircserv"),
			_createdAt("(startup)"),
			_startedAt(std::time(0)),
			_totalConnections(0),
			_peakClients(0),
			_clients(),
			_channels(),
			_pollfds(),
			_dispatcher() {
	std::time_t now = std::time(0);
	std::tm* tmv = std::localtime(&now);
	char buf[64];
	if (tmv != 0
			&& std::strftime(buf, sizeof(buf), "%a %b %d %Y %H:%M:%S", tmv) > 0) {
		_createdAt = buf;
	}
}

Server::~Server() {
	for (std::map<int, Client*>::iterator it = _clients.begin();
			it != _clients.end(); ++it) {
		close(it->first);
		delete it->second;
	}
	_clients.clear();

	for (std::map<std::string, Channel*>::iterator it = _channels.begin();
			it != _channels.end(); ++it) {
		delete it->second;
	}
	_channels.clear();
}

Client* Server::findClientByNick(const std::string& nick) {
	std::string key = StringUtil::ircCaseFold(nick);
	for (std::map<int, Client*>::iterator it = _clients.begin();
			it != _clients.end(); ++it) {
		if (it->second->hasNick()
				&& StringUtil::ircCaseFold(it->second->nick()) == key) {
			return it->second;
		}
	}
	return 0;
}

Channel* Server::findChannel(const std::string& name) {
	std::map<std::string, Channel*>::iterator it =
			_channels.find(StringUtil::ircCaseFold(name));
	if (it == _channels.end()) {
		return 0;
	}
	return it->second;
}

Channel* Server::getOrCreateChannel(const std::string& name, Client& creator) {
	std::string key = StringUtil::ircCaseFold(name);
	std::map<std::string, Channel*>::iterator it = _channels.find(key);
	if (it != _channels.end()) {
		return it->second;
	}
	Channel* channel = new Channel(name, creator);
	try {
		std::pair<std::map<std::string, Channel*>::iterator, bool> inserted =
				_channels.insert(std::make_pair(key, channel));
		if (!inserted.second) {
			channel->removeMember(creator);
			delete channel;
			return inserted.first->second;
		}
	}
	catch (...) {
		channel->removeMember(creator);
		delete channel;
		throw;
	}
	Log::chan("# " + channel->name() + " created by " + Log::who(creator)
			+ " (channels: " + StringUtil::toString(
					static_cast<long>(_channels.size())) + ")");
	return channel;
}

void Server::removeEmptyChannel(Channel* channel) {
	if (channel == 0 || !channel->isEmpty()) {
		return;
	}
	const std::string name = channel->name();
	_channels.erase(StringUtil::ircCaseFold(name));
	delete channel;
	Log::chan("# " + name + " destroyed, last member left (channels: "
			+ StringUtil::toString(static_cast<long>(_channels.size())) + ")");
}

const std::string& Server::password() const {
	return _password;
}

const std::string& Server::serverName() const {
	return _serverName;
}

const std::string& Server::createdAt() const {
	return _createdAt;
}

void Server::requestStop(int signum) {
	(void)signum;
	_running = 0;
}

void Server::setup() {
	_listen.openListen(_port);
}

void Server::logStartup() const {
	Log::banner(_serverName, SERVER_VERSION);
	Log::field("port", StringUtil::toString(_port));
	Log::field("password", "set ("
			+ StringUtil::toString(static_cast<long>(_password.size()))
			+ " chars)");
	Log::field("started", _createdAt);
	Log::field("limits", std::string("accept 1/poll-ready event  reg timeout ")
			+ StringUtil::toString(static_cast<long>(REG_TIMEOUT_SEC))
			+ "s  poll " + StringUtil::toString(POLL_TIMEOUT_MS) + "ms");
	Log::field("log", std::string("per-message trace ")
			+ (Log::traceEnabled() ? "on" : "off (set IRC_TRACE=1)"));
	Log::rule();
	Log::info("listening on 0.0.0.0:" + StringUtil::toString(_port));
	Log::info("waiting for clients (^C to stop)");
}

void Server::logShutdown() const {
	Log::info("signal received, shutting down");
	Log::info("uptime " + formatDuration(std::time(0) - _startedAt)
			+ "  connections " + StringUtil::toString(
					static_cast<long>(_totalConnections))
			+ " (peak " + StringUtil::toString(static_cast<long>(_peakClients))
			+ " at once)  open channels "
			+ StringUtil::toString(static_cast<long>(_channels.size())));
	if (Log::droppedLines() > 0) {
		Log::warn("dropped " + StringUtil::toString(
				static_cast<long>(Log::droppedLines()))
				+ " log line(s): stdout could not keep up");
	}
	Log::info("closing " + StringUtil::toString(
			static_cast<long>(_clients.size())) + " client socket(s), bye");
}

void Server::rebuildPollFds() {
	_pollfds.clear();

	struct pollfd listenPfd;
	listenPfd.fd = _listen.fd();
	listenPfd.events = POLLIN;
	listenPfd.revents = 0;
	_pollfds.push_back(listenPfd);

	if (Log::hasPending()) {
		struct pollfd logPfd;
		logPfd.fd = STDOUT_FILENO;
		logPfd.events = POLLOUT;
		logPfd.revents = 0;
		_pollfds.push_back(logPfd);
	}

	for (std::map<int, Client*>::iterator it = _clients.begin();
			it != _clients.end(); ++it) {
		struct pollfd pfd;
		pfd.fd = it->first;
		pfd.events = 0;
		if (!it->second->isReadClosed()) {
			pfd.events |= POLLIN;
		}
		if (it->second->hasPendingOutput()) {
			pfd.events |= POLLOUT;
		}
		pfd.revents = 0;
		_pollfds.push_back(pfd);
	}
}

void Server::sweepClients() {
	std::time_t now = std::time(0);
	std::vector<int> overflow;
	std::vector<int> regTimeout;
	std::vector<int> staleClose;
	for (std::map<int, Client*>::iterator it = _clients.begin();
			it != _clients.end(); ++it) {
		Client* client = it->second;
		if (client->isReadClosed()) {
			if (now - client->closingSince() >= CLOSE_TIMEOUT_SEC) {
				staleClose.push_back(it->first);
			}
			continue;
		}
		if (client->outputOverflow()) {
			overflow.push_back(it->first);
		} else if (!client->isRegistered()
				&& now - client->connectedAt() >= REG_TIMEOUT_SEC) {
			regTimeout.push_back(it->first);
		}
	}
	for (std::size_t i = 0; i < overflow.size(); ++i) {
		std::map<int, Client*>::iterator it = _clients.find(overflow[i]);
		if (it != _clients.end()) {
			disconnect(*it->second, "send queue exceeded");
		}
	}
	for (std::size_t i = 0; i < regTimeout.size(); ++i) {
		std::map<int, Client*>::iterator it = _clients.find(regTimeout[i]);
		if (it != _clients.end()) {
			gracefulClose(*it->second, "registration timeout");
		}
	}
	for (std::size_t i = 0; i < staleClose.size(); ++i) {
		std::map<int, Client*>::iterator it = _clients.find(staleClose[i]);
		if (it != _clients.end()) {
			disconnect(*it->second, "close timeout");
		}
	}
}

void Server::run() {
	setup();
	Log::reserve();
	_running = 1;

	std::signal(SIGINT, Server::requestStop);
	std::signal(SIGTERM, Server::requestStop);
	std::signal(SIGPIPE, SIG_IGN);

	logStartup();

	bool stopping = false;
	std::time_t drainDeadline = 0;

	while (true) {
		if (!_running && !stopping) {
			logShutdown();
			stopping = true;
			drainDeadline = std::time(0) + LOG_DRAIN_SEC;
		}
		if (stopping
				&& (!Log::hasPending() || std::time(0) >= drainDeadline)) {
			break;
		}
		try {
			rebuildPollFds();

			nfds_t nfds = static_cast<nfds_t>(_pollfds.size());
			int ready = poll(&_pollfds[0], nfds, POLL_TIMEOUT_MS);
			if (ready < 0) {
				if (errno == EINTR) {
					continue;
				}
				throw std::runtime_error(
						std::string("poll: ") + std::strerror(errno));
			}

			for (std::size_t i = 0; i < _pollfds.size(); ++i) {
				short re = _pollfds[i].revents;
				if (re == 0) {
					continue;
				}

				int fd = _pollfds[i].fd;

				if (fd == _listen.fd()) {
					if (re & POLLIN) {
						acceptClient();
					}
					if (re & (POLLERR | POLLHUP | POLLNVAL)) {
						throw std::runtime_error("listening socket poll error");
					}
					continue;
				}

				if (fd == STDOUT_FILENO) {
					if (re & POLLOUT) {
						Log::drainOnce();
					}
					continue;
				}

				std::map<int, Client*>::iterator it = _clients.find(fd);
				if (it == _clients.end()) {
					continue;
				}
				try {
					if (re & POLLIN) {
						handleReadable(fd);
						it = _clients.find(fd);
						if (it == _clients.end()) {
							continue;
						}
					}
					if (re & POLLOUT) {
						handleWritable(fd);
						it = _clients.find(fd);
						if (it == _clients.end()) {
							continue;
						}
						if (it->second->isReadClosed()
								&& !it->second->hasPendingOutput()) {
							disconnect(*it->second, "client closed connection");
							continue;
						}
					}
					if (re & (POLLERR | POLLHUP | POLLNVAL)) {
						it = _clients.find(fd);
						if (it != _clients.end()) {
							disconnect(*it->second, "poll error/hangup");
						}
					}
				}
				catch (const std::bad_alloc&) {
					std::map<int, Client*>::iterator jt = _clients.find(fd);
					if (jt != _clients.end()) {
						dropQuietly(*jt->second);
					}
				}
			}

			sweepClients();
		}
		catch (const std::bad_alloc&) {
			if (!_clients.empty()) {
				dropQuietly(*_clients.begin()->second);
			}
			continue;
		}
	}
}

void Server::acceptClient() {
	std::string host;
	int fd = _listen.acceptClient(host);
	if (fd < 0) {
		return;
	}
	Client* client = 0;
	try {
		client = new Client(fd, host);
		_clients[fd] = client;
	}
	catch (...) {
		close(fd);
		delete client;
		Log::oomWarn("dropped a connection: client allocation failed");
		return;
	}
	++_totalConnections;
	if (_clients.size() > _peakClients) {
		_peakClients = _clients.size();
	}
	Log::conn("+ " + host + " fd " + StringUtil::toString(fd)
			+ " (clients: " + StringUtil::toString(
					static_cast<long>(_clients.size())) + ")");
}

void Server::handleReadable(int fd) {
	std::map<int, Client*>::iterator it = _clients.find(fd);
	if (it == _clients.end()) {
		return;
	}
	Client& client = *it->second;
	char buf[READ_CHUNK];

	ssize_t n = recv(client.fd(), buf, sizeof(buf), 0);
	if (n < 0) {
		disconnect(client, "recv error");
		return;
	}
	if (n > 0) {
		client.appendInput(buf, static_cast<std::size_t>(n));
		pumpLines(fd);
	}

	it = _clients.find(fd);
	if (it == _clients.end()) {
		return;
	}
	Client& current = *it->second;
	if (current.isReadClosed()) {
		return;
	}

	if (current.inputOverflow()) {
		gracefulClose(current,
				"input line too long "
				"(RFC 2812: messages must be <=512 octets including CR-LF)");
		return;
	}
	if (current.outputOverflow()) {
		disconnect(current, "send queue exceeded");
		return;
	}

	if (n == 0) {
		if (current.hasPendingOutput()) {
			current.markReadClosed();
		} else {
			disconnect(current, "client closed connection");
		}
	}
}

void Server::handleWritable(int fd) {
	std::map<int, Client*>::iterator it = _clients.find(fd);
	if (it == _clients.end()) {
		return;
	}
	Client& client = *it->second;
	std::string& out = client.outBuffer();
	if (out.empty()) {
		return;
	}

	ssize_t n = send(client.fd(), out.c_str(), out.size(), 0);
	if (n <= 0) {
		disconnect(client, "send error");
		return;
	}
	out.erase(0, static_cast<std::size_t>(n));
}

void Server::pumpLines(int fd) {
	std::string line;
	while (true) {
		std::map<int, Client*>::iterator it = _clients.find(fd);
		if (it == _clients.end()) {
			return;
		}
		Client& client = *it->second;
		if (!client.extractLine(line)) {
			return;
		}
		if (client.inputProtocolError()) {
			gracefulClose(client,
					"invalid message format or line too long "
					"(RFC 2812: messages must be CR-LF terminated, <=512 octets)");
			return;
		}
		Message msg = Message::parse(line);
		if (msg.empty()) {
			continue;
		}
		_dispatcher.dispatch(*this, client, msg);
		it = _clients.find(fd);
		if (it == _clients.end()) {
			return;
		}
		if (it->second->isReadClosed()) {
			return;
		}
	}
}

void Server::sendLine(Client& client, const std::string& line) {
	std::string queued = StringUtil::capLine(line) + IRC_CRLF;
	client.appendOutput(queued);
}

void Server::completeRegistration(Client& client) {
	if (client.isRegistered() || !client.passAccepted()
			|| !client.hasNick() || !client.hasUser()) {
		return;
	}
	client.markRegistered();
	Log::auth("* " + client.prefix() + " registered (fd "
			+ StringUtil::toString(client.fd()) + ", realname \""
			+ client.realname() + "\")");

	const std::string& name = _serverName;
	const std::string& nick = client.nick();
	sendLine(client, Reply::numeric(name, Reply::RPL_WELCOME, nick,
			":Welcome to the Internet Relay Network " + client.prefix()));
	sendLine(client, Reply::numeric(name, Reply::RPL_YOURHOST, nick,
			":Your host is " + name + ", running version " + SERVER_VERSION));
	sendLine(client, Reply::numeric(name, Reply::RPL_CREATED, nick,
			":This server was created " + _createdAt));
	sendLine(client, Reply::numeric(name, Reply::RPL_MYINFO, nick,
			name + " " + SERVER_VERSION + " - itkol"));
}

void Server::announceQuit(Client& client, const std::string& reason) {
	const std::string quitLine =
			Reply::from(client.prefix(), "QUIT :" + reason);

	std::set<Client*> recipients;

	std::map<std::string, Channel*>::iterator it = _channels.begin();
	while (it != _channels.end()) {
		Channel* channel = it->second;
		if (channel->hasMember(client)) {
			const std::set<Client*>& mem = channel->members();
			recipients.insert(mem.begin(), mem.end());
		}
		channel->removeMember(client);
		if (channel->isEmpty()) {
			const std::string name = channel->name();
			delete channel;
			_channels.erase(it++);
			Log::chan("# " + name + " destroyed, last member left (channels: "
					+ StringUtil::toString(static_cast<long>(_channels.size()))
					+ ")");
		} else {
			++it;
		}
	}

	recipients.erase(&client);

	for (std::set<Client*>::iterator r = recipients.begin();
			r != recipients.end(); ++r) {
		sendLine(**r, quitLine);
	}
}

void Server::finalize(Client& client) {
	int fd = client.fd();
	_clients.erase(fd);
	close(fd);
	delete &client;
}

void Server::dropQuietly(Client& client) {
	std::map<std::string, Channel*>::iterator it = _channels.begin();
	while (it != _channels.end()) {
		Channel* channel = it->second;
		channel->removeMember(client);
		if (channel->isEmpty()) {
			delete channel;
			_channels.erase(it++);
		} else {
			++it;
		}
	}
	Log::oomWarn("dropped a client to recover memory:", client.host().c_str());
	finalize(client);
}

void Server::disconnect(Client& client, const std::string& reason) {
	const std::string label = Log::who(client);
	announceQuit(client, reason);
	finalize(client);
	Log::conn("- " + label + " disconnected: " + reason + " (clients: "
			+ StringUtil::toString(static_cast<long>(_clients.size())) + ")");
}

void Server::gracefulClose(Client& client, const std::string& reason) {
	announceQuit(client, reason);
	client.appendOutput(buildErrorLine(client.host(), reason));
	client.markReadClosed();
	Log::conn("~ " + Log::who(client) + " closing: " + reason + " (flushing "
			+ StringUtil::toString(static_cast<long>(client.outBuffer().size()))
			+ "B)");
}
