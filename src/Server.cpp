#include "Server.hpp"

#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstring>
#include <ctime>

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
	const int POLL_TIMEOUT_MS = 1000;       // 掃引を回すためpollは有限待ち
	const std::time_t REG_TIMEOUT_SEC = 60;  // connectからこの秒数で登録未完なら切断
	const std::time_t CLOSE_TIMEOUT_SEC = 10;  // 猶予切断のflushがこの秒数で終わらなければ強制finalize
	const int MAX_ACCEPT = 16;

	// nick/チャンネル名はcase-insensitive（ASCIIのみ）で照合する
	std::string lowerAscii(const std::string& s) {
		std::string r(s);
		for (std::string::size_type i = 0; i < r.size(); ++i) {
			r[i] = static_cast<char>(
					std::tolower(static_cast<unsigned char>(r[i])));
		}
		return r;
	}

	// ログ用の稼働時間表記（1h2m3s / 2m3s / 3s）
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

	// reason を §2.3.1（NUL/CR/LF不可）で無害化し，§2.3（≤512, CRLF含む）に収めた ERROR 行を作る
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
		if (line.size() > 510) {  // 510 = 512 - CRLF
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
	std::string key = lowerAscii(nick);
	for (std::map<int, Client*>::iterator it = _clients.begin();
			it != _clients.end(); ++it) {
		if (it->second->hasNick() && lowerAscii(it->second->nick()) == key) {
			return it->second;
		}
	}
	return 0;
}

Channel* Server::findChannel(const std::string& name) {
	std::map<std::string, Channel*>::iterator it =
			_channels.find(lowerAscii(name));
	if (it == _channels.end()) {
		return 0;
	}
	return it->second;
}

Channel* Server::getOrCreateChannel(const std::string& name, Client& creator) {
	std::string key = lowerAscii(name);
	std::map<std::string, Channel*>::iterator it = _channels.find(key);
	if (it != _channels.end()) {
		return it->second;
	}
	Channel* channel = new Channel(name, creator);
	_channels[key] = channel;
	Log::chan("# " + channel->name() + " created by " + Log::who(creator)
			+ " (channels: " + StringUtil::toString(
					static_cast<long>(_channels.size())) + ")");
	return channel;
}

void Server::removeEmptyChannel(Channel* channel) {
	if (channel == 0 || !channel->isEmpty()) {
		return;
	}
	const std::string name = channel->name();   // delete後に使うのでコピー
	_channels.erase(lowerAscii(name));
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

// 起動バナー＋設定パネル．listen 成功後に呼ぶ（失敗時は Fatal だけ出す）
void Server::logStartup() const {
	Log::banner(_serverName, SERVER_VERSION);
	Log::field("port", StringUtil::toString(_port));
	Log::field("password", "set ("
			+ StringUtil::toString(static_cast<long>(_password.size()))
			+ " chars)");
	Log::field("started", _createdAt);
	Log::field("limits", "accept "
			+ StringUtil::toString(MAX_ACCEPT) + "/loop  reg timeout "
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

// poll1周ごとに全クライアントを掃引．出力上限超過（#43）と未登録タイムアウト（#44）を切断．
// POLLINが来ない純受信クライアントもここで確実に掃引される．
void Server::sweepClients() {
	std::time_t now = std::time(0);
	std::vector<int> overflow;
	std::vector<int> regTimeout;
	std::vector<int> staleClose;
	for (std::map<int, Client*>::iterator it = _clients.begin();
			it != _clients.end(); ++it) {
		Client* client = it->second;
		if (client->isReadClosed()) {
			// 猶予切断中: flushがCLOSE_TIMEOUT_SECで終わらなければ強制finalize（リンガーfd有界化）
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
	// disconnectは_clientsを変更するので走査後にまとめて実施
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
	// flushできずに居座る猶予切断中クライアントを強制切断（disconnectは未通知のEOFにも対応）
	for (std::size_t i = 0; i < staleClose.size(); ++i) {
		std::map<int, Client*>::iterator it = _clients.find(staleClose[i]);
		if (it != _clients.end()) {
			disconnect(*it->second, "close timeout");
		}
	}
}

void Server::run() {
	setup();
	_running = 1;

	std::signal(SIGINT, Server::requestStop);
	std::signal(SIGTERM, Server::requestStop);
	std::signal(SIGPIPE, SIG_IGN);

	logStartup();

	while (_running) {
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
				continue;
			}

			std::map<int, Client*>::iterator it = _clients.find(fd);
			if (it == _clients.end()) {
				continue;
			}
			Client* client = it->second;

			// NOLINTBEGIN(clang-analyzer-cplusplus.NewDelete): disconnect 後は find(fd)==end() で
			// 必ず continue するため UAF にならない（解析器の誤検知）

			if (re & POLLIN) {
				handleReadable(*client);
				if (_clients.find(fd) == _clients.end()) {
					continue;
				}
			}
			if (re & POLLOUT) {
				handleWritable(*client);
				if (_clients.find(fd) == _clients.end()) {
					continue;
				}
				if (client->isReadClosed() && !client->hasPendingOutput()) {
					disconnect(*client, "client closed connection");
					continue;
				}
			}
			if (re & (POLLERR | POLLHUP | POLLNVAL)) {
				if (!(client->isReadClosed() && client->hasPendingOutput())) {
					disconnect(*client, "poll error/hangup");
				}
			}
			// NOLINTEND(clang-analyzer-cplusplus.NewDelete)
		}

		sweepClients();
	}

	logShutdown();
}

void Server::acceptClient() {
	std::string host;
	int accepted = 0;
	while (accepted < MAX_ACCEPT) {
		int fd = _listen.acceptClient(host);
		if (fd < 0 ) {
			break;
		}
		Client* client = 0;
		try {
			client = new Client(fd, host);
		}
		catch (...) {
			close(fd);
			// ここはメモリ不足の経路。通常ログは std::string を組むので使えない
			Log::oomWarn("dropped a connection: client allocation failed");
			continue;
		}
		_clients[fd] = client;
		accepted++;
		++_totalConnections;
		if (_clients.size() > _peakClients) {
			_peakClients = _clients.size();
		}
		Log::conn("+ " + host + " fd " + StringUtil::toString(fd)
				+ " (clients: " + StringUtil::toString(
						static_cast<long>(_clients.size())) + ")");
	}
}

void Server::handleReadable(Client& client) {
	char buf[READ_CHUNK];
	ssize_t n = recv(client.fd(), buf, sizeof(buf), 0);
	while (n > 0) {
		client.appendInput(buf, static_cast<std::size_t>(n));
		n = recv(client.fd(), buf, sizeof(buf), 0);
	}
	// errno は pumpLines 内の send 等で上書きされる前に確保する
	int recvErrno = errno;

	int fd = client.fd();
	pumpLines(client);
	if (_clients.find(fd) == _clients.end()) {
		return;
	}
	// QUIT等で猶予切断が始まったら，残りの入力/EOF処理はせず flush→finalize に任せる
	if (client.isReadClosed()) {
		return;
	}

	if (client.inputOverflow()) {
		disconnect(client, "input line too long");
		return;
	}
	if (client.outputOverflow()) {
		disconnect(client, "send queue exceeded");
		return;
	}

	if (n == 0) {
		if (client.hasPendingOutput()) {
			client.markReadClosed();
		} else {
			disconnect(client, "client closed connection");
		}
	} else if (n < 0 && recvErrno != EAGAIN && recvErrno != EWOULDBLOCK
			&& recvErrno != EINTR) {
		disconnect(client, "recv error");
	}
}

void Server::handleWritable(Client& client) {
	std::string& out = client.outBuffer();
	if (out.empty()) {
		return;
	}

	ssize_t n = send(client.fd(), out.c_str(), out.size(), 0);
	if (n > 0) {
		out.erase(0, static_cast<std::size_t>(n));
	} else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK
			&& errno != EINTR) {
		disconnect(client, "send error");
	}
	// EAGAIN/EWOULDBLOCK/EINTR と n==0 はバッファ保持で次の POLLOUT に回す
}

void Server::pumpLines(Client& client) {
	int fd = client.fd();
	std::string line;
	while (client.extractLine(line)) {
		Message msg = Message::parse(line);
		if (msg.empty()) {
			continue;
		}
		_dispatcher.dispatch(*this, client, msg);
		// dispatch中にQUIT等で切断済みならclientは解放されている
		if (_clients.find(fd) == _clients.end()) {
			return;
		}
		// QUIT等で猶予切断が始まったら，同パケットの後続行は処理しない
		if (client.isReadClosed()) {
			return;
		}
	}
}

void Server::queueMessage(Client& client, const std::string& message) {
	client.appendOutput(message);
}

// 1行をCRLF終端で送信キューへ積む。コマンドはこちらを使う（queueMessageは生バイト用）
void Server::sendLine(Client& client, const std::string& line) {
	client.appendOutput(StringUtil::capLine(line) + IRC_CRLF);
}

// PASS/NICK/USER が処理後に呼ぶ共通ロジック。pass/nick/user が揃うまでは何もしない。
// 揃った瞬間に登録完了扱いにしてウェルカム001-004を順に送る（多重送出は !isRegistered で防止）。
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
			name + " " + SERVER_VERSION + " o itkol"));
	// 005 ISUPPORT: 照合はASCII固定なのでCASEMAPPING=asciiを明示（鍵+kはcase-sensitiveのまま）
	sendLine(client, Reply::numeric(name, Reply::RPL_ISUPPORT, nick,
			"CASEMAPPING=ascii CHANTYPES=# CHANMODES=,k,l,it PREFIX=(o)@ "
			"CHANNELLEN=50 NICKLEN=9 :are supported by this server"));
}

// 共有チャンネルを持つ各クライアントへ QUIT を1回だけ通知し，全チャンネルから除去する（本人は除外）。
void Server::announceQuit(Client& client, const std::string& reason) {
	const std::string quitLine =
			Reply::from(client.prefix(), "QUIT :" + reason);

	std::set<Client*> recipients;

	// 参加中だけでなく invited のみのチャンネルにも生ポインタが残るため全チャンネルを走査し removeMember する
	std::map<std::string, Channel*>::iterator it = _channels.begin();
	while (it != _channels.end()) {
		Channel* channel = it->second;
		// removeMember 前に収集する。removeMember 後は hasMember(client) が必ず偽になり，
		// そのチャンネルを配送対象と判定できなくなるため（単独メンバーなら空削除もされる）。
		if (channel->hasMember(client)) {
			const std::set<Client*>& mem = channel->members();
			recipients.insert(mem.begin(), mem.end());
		}
		channel->removeMember(client);
		if (channel->isEmpty()) {
			const std::string name = channel->name();   // delete後に使うのでコピー
			delete channel;
			_channels.erase(it++);
			Log::chan("# " + name + " destroyed, last member left (channels: "
					+ StringUtil::toString(static_cast<long>(_channels.size()))
					+ ")");
		} else {
			++it;
		}
	}

	// 本人は QUIT を受け取らない（本人へは別途 ERROR を送る）。集合構築後に1回除外。
	recipients.erase(&client);

	// 共有チャンネルを持つ各相手へちょうど1回。sendLine は broadcast と同一バイト列を積む。
	for (std::set<Client*>::iterator r = recipients.begin();
			r != recipients.end(); ++r) {
		sendLine(**r, quitLine);
	}
}

// fdをpollから外して実際に閉じ，Clientを破棄する。以降そのfdは _clients に無い。
void Server::finalize(Client& client) {
	int fd = client.fd();
	_clients.erase(fd);
	close(fd);
	delete &client;
}

// 即時切断。ソケットが死んでいる/送信バッファ満杯でERRORを送れない経路用（ERRORは付けない）。
void Server::disconnect(Client& client, const std::string& reason) {
	const std::string label = Log::who(client);   // finalize後は client が無いので先に取る
	announceQuit(client, reason);
	finalize(client);
	Log::conn("- " + label + " disconnected: " + reason + " (clients: "
			+ StringUtil::toString(static_cast<long>(_clients.size())) + ")");
}

// 猶予切断: RFC2812 §3.7.4/§3.1.7 の ERROR を _outBuf に積み，POLLOUT で送り切ってから
// finalize する（N11: 送信は必ずPOLLOUT経由）。読みは止める。QUIT・登録タイムアウト用。
void Server::gracefulClose(Client& client, const std::string& reason) {
	announceQuit(client, reason);
	client.appendOutput(buildErrorLine(client.host(), reason));
	client.markReadClosed();
	Log::conn("~ " + Log::who(client) + " closing: " + reason + " (flushing "
			+ StringUtil::toString(static_cast<long>(client.outBuffer().size()))
			+ "B)");
}
