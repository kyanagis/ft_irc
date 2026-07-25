#include "Server.hpp"

#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstring>
#include <ctime>

#include <new>
#include <stdexcept>

#include <sys/socket.h>
#include <unistd.h>

#include "Channel.hpp"
#include "Client.hpp"
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
	const std::size_t MAX_READ_BYTES_PER_EVENT = 64UL * 1024;
	const int MAX_RECV_PER_EVENT = 16;

	// RFC 2812 §2.2 のcasemapping。ASCII英字に加え、{}|^ は []\~ と
	// それぞれ同一視する。チャンネル鍵の比較には使用しない。
	std::string ircCaseFold(const std::string& s) {
		std::string r(s);
		for (std::string::size_type i = 0; i < r.size(); ++i) {
			if (r[i] >= 'A' && r[i] <= 'Z') {
				r[i] = static_cast<char>(r[i] - 'A' + 'a');
			} else if (r[i] == '{') {
				r[i] = '[';
			} else if (r[i] == '}') {
				r[i] = ']';
			} else if (r[i] == '|') {
				r[i] = '\\';
			} else if (r[i] == '^') {
				r[i] = '~';
			}
		}
		return r;
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
	std::string key = ircCaseFold(nick);
	for (std::map<int, Client*>::iterator it = _clients.begin();
			it != _clients.end(); ++it) {
		if (it->second->hasNick()
				&& ircCaseFold(it->second->nick()) == key) {
			return it->second;
		}
	}
	return 0;
}

Channel* Server::findChannel(const std::string& name) {
	std::map<std::string, Channel*>::iterator it =
			_channels.find(ircCaseFold(name));
	if (it == _channels.end()) {
		return 0;
	}
	return it->second;
}

Channel* Server::getOrCreateChannel(const std::string& name, Client& creator) {
	std::string key = ircCaseFold(name);
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
	return channel;
}

void Server::removeEmptyChannel(Channel* channel) {
	if (channel == 0 || !channel->isEmpty()) {
		return;
	}
	_channels.erase(ircCaseFold(channel->name()));
	delete channel;
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

	while (_running) {
		// メモリ枯渇(bad_alloc)でプロセスを落とさない（subject: OOMでも予期せぬ終了は不可）。
		// 1周分の割当(pollfd構築/accept/per-client/sweep)をまとめて守り，枯渇した周は捨てて
		// 次周で再試行する。poll失敗のruntime_errorはbad_allocでないのでここは素通りし，
		// 意図通り致命になる。per-client処理は内側tryで個別にdropして周内の他fdへ影響させない。
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
						// POLLHUPはeventsの指定に関係なく繰り返し返る。保留出力を
						// 待って無視するとclose timeoutまでbusy loopになるため、
						// この周のPOLLOUT送信を一度試した後は即時切断する。
						it = _clients.find(fd);
						if (it != _clients.end()) {
							disconnect(*it->second, "poll error/hangup");
						}
					}
				}
				catch (const std::bad_alloc&) {
					// このクライアント処理中にメモリ枯渇。該当clientをdropしてバッファを
					// 解放し継続する（disconnectはannounceQuitのbroadcastでbad_allocを
					// 再throwし得るので使わず，nothrowなdropQuietlyで落とす）。
					// disconnect途中でthrowしていてもfindで残存を確認してから処理する。
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
	int accepted = 0;
	while (accepted < MAX_ACCEPT) {
		int fd = _listen.acceptClient(host);
		if (fd < 0) {
			break;
		}
		Client* client = 0;
		try {
			client = new Client(fd, host);
			_clients[fd] = client;  // map挿入もtry内: bad_allocでもfd/clientを漏らさない
		}
		catch (...) {
			close(fd);
			delete client;  // new成功後にinsertが投げた時のみ非0。new失敗時は0でdelete安全
			continue;
		}
		accepted++;
	}
}

void Server::handleReadable(int fd) {
	std::map<int, Client*>::iterator it = _clients.find(fd);
	if (it == _clients.end()) {
		return;
	}
	Client& client = *it->second;
	char buf[READ_CHUNK];
	ssize_t n = -1;
	std::size_t bytesRead = 0;
	int recvCount = 0;
	int recvErrno = 0;

	// 常に送信し続ける1クライアントが他fdをstarveさせないよう、pollイベント
	// 1回あたりのrecv回数と総byte数を制限する。未読データは次周もPOLLINになる。
	while (recvCount < MAX_RECV_PER_EVENT
			&& bytesRead < MAX_READ_BYTES_PER_EVENT) {
		std::size_t remaining = MAX_READ_BYTES_PER_EVENT - bytesRead;
		std::size_t request = remaining < sizeof(buf) ? remaining : sizeof(buf);
		n = recv(client.fd(), buf, request, 0);
		++recvCount;
		if (n <= 0) {
			if (n < 0) {
				recvErrno = errno;
			}
			break;
		}
		client.appendInput(buf, static_cast<std::size_t>(n));
		bytesRead += static_cast<std::size_t>(n);
	}

	pumpLines(fd);
	it = _clients.find(fd);
	if (it == _clients.end()) {
		return;
	}
	Client& current = *it->second;
	// QUIT等で猶予切断が始まったら，残りの入力/EOF処理はせず flush→finalize に任せる
	if (current.isReadClosed()) {
		return;
	}

	if (current.inputOverflow()) {
		disconnect(current, "input line too long");
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
	} else if (n < 0 && recvErrno != EAGAIN && recvErrno != EWOULDBLOCK
			&& recvErrno != EINTR) {
		disconnect(current, "recv error");
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
	if (n > 0) {
		out.erase(0, static_cast<std::size_t>(n));
	} else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK
			&& errno != EINTR) {
		disconnect(client, "send error");
	}
	// EAGAIN/EWOULDBLOCK/EINTR と n==0 はバッファ保持で次の POLLOUT に回す
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
			disconnect(client, "invalid message format or line too long");
			return;
		}
		Message msg = Message::parse(line);
		if (msg.empty()) {
			continue;
		}
		_dispatcher.dispatch(*this, client, msg);
		// dispatch中にQUIT等で切断済みならclientは解放されている
		it = _clients.find(fd);
		if (it == _clients.end()) {
			return;
		}
		// QUIT等で猶予切断が始まったら，同パケットの後続行は処理しない
		if (it->second->isReadClosed()) {
			return;
		}
	}
}

void Server::queueMessage(Client& client, const std::string& message) {
	client.appendOutput(message);
}

// 1行をCRLF終端で送信キューへ積む。コマンドはこちらを使う（queueMessageは生バイト用）
void Server::sendLine(Client& client, const std::string& line) {
	std::string queued = StringUtil::capLine(line) + IRC_CRLF;
	client.appendOutput(queued);
}

// PASS/NICK/USER が処理後に呼ぶ共通ロジック。pass/nick/user が揃うまでは何もしない。
// 揃った瞬間に登録完了扱いにしてウェルカム001-004を順に送る（多重送出は !isRegistered で防止）。
void Server::completeRegistration(Client& client) {
	if (client.isRegistered() || !client.passAccepted()
			|| !client.hasNick() || !client.hasUser()) {
		return;
	}
	client.markRegistered();

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

// 参加中の各チャンネルへ QUIT を1回ずつ通知し，全チャンネルから除去する（本人は除外）。
void Server::announceQuit(Client& client, const std::string& reason) {
	const std::string quitLine = Reply::from(client.prefix(), "QUIT :" + reason);

	// 参加中だけでなくinvitedのみのチャンネルにも生ポインタが残るので全走査
	std::map<std::string, Channel*>::iterator it = _channels.begin();
	while (it != _channels.end()) {
		Channel* channel = it->second;
		if (channel->hasMember(client))
			channel->broadcast(quitLine, &client);
		channel->removeMember(client);
		if (channel->isEmpty()) {
			delete channel;
			_channels.erase(it++);
		} else {
			++it;
		}
	}
}

// fdをpollから外して実際に閉じ，Clientを破棄する。以降そのfdは _clients に無い。
void Server::finalize(Client& client) {
	int fd = client.fd();
	_clients.erase(fd);
	close(fd);
	delete &client;
}

// メモリ枯渇時などの緊急切断用。QUIT通知(broadcastは再割当でbad_allocを再throwし得る)を
// 出さず，全チャンネルから除去してからfinalizeする。removeMember/erase/close/deleteは
// いずれも割当を伴わないので本関数はnothrow。単なるfinalizeだけだとChannelに生ポインタが
// 残りUAFになるため，チャンネル除去を必ず先に行う。
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
	finalize(client);
}

// 即時切断。ソケットが死んでいる/送信バッファ満杯でERRORを送れない経路用（ERRORは付けない）。
void Server::disconnect(Client& client, const std::string& reason) {
	announceQuit(client, reason);
	finalize(client);
}

// 猶予切断: RFC2812 §3.7.4/§3.1.7 の ERROR を _outBuf に積み，POLLOUT で送り切ってから
// finalize する（N11: 送信は必ずPOLLOUT経由）。読みは止める。QUIT・登録タイムアウト用。
void Server::gracefulClose(Client& client, const std::string& reason) {
	announceQuit(client, reason);
	client.appendOutput(buildErrorLine(client.host(), reason));
	client.markReadClosed();
}
