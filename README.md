*This project has been created as part of the 42 curriculum by kyanagis, kemotoha, keitabe.*

# ft_irc

## Description

`ft_irc` is an IRC server written from scratch in C++98, without any external
library. It speaks the client half of the IRC protocol (RFC 1459 / RFC 2812)
well enough that a real reference client — `irssi`, HexChat, WeeChat or plain
`nc` — can connect to it, register, join channels and talk, just like it would
with an official IRC server.

The goal of the project is not RFC completeness but the network programming
underneath it: a **single-process, single-threaded server** that multiplexes
every socket through **one `poll()` call**, keeps every file descriptor
non-blocking, never forks, and never crashes — not on malformed input, not on a
half-sent command, not when the machine runs out of memory.

Server-to-server communication (RFC 2813) and any kind of IRC client are outside
the scope of the subject and are not implemented.

## Instructions

### Requirements

- A C++ compiler supporting C++98 (`clang++` or `g++`)
- `make`
- Linux or macOS

### Build

```sh
make          # builds ./ircserv
make clean    # removes object files
make fclean   # removes object files and the binary
make re       # fclean + all
```

The build uses `-Wall -Wextra -Werror -std=c++98 -pedantic-errors` and header
dependency tracking (`-MMD -MP`), so editing a header only rebuilds what
actually depends on it.

### Run

```sh
./ircserv <port> <password>
```

- `<port>` — TCP port to listen on, an integer in `[1, 65535]`
- `<password>` — connection password, must not be empty; clients must send it
  with `PASS` before registering

Example:

```sh
./ircserv 6667 mypassword
```

The server listens on `0.0.0.0` (IPv4) and prints a startup banner followed by a
live event log. `Ctrl-C` (`SIGINT`) or `SIGTERM` shuts it down cleanly: the
remaining sockets are closed and all memory is released.

Two environment variables affect the log only:

| Variable | Effect |
| --- | --- |
| `IRC_TRACE=1` | Also log every received command and every relayed message |
| `NO_COLOR=1` | Disable ANSI colors (also honoured when `TERM=dumb`) |

### Connecting

With a reference client:

```sh
irssi -c 127.0.0.1 -p 6667 -w mypassword
```

With `nc`, typing the registration sequence by hand:

```sh
nc -C 127.0.0.1 6667
PASS mypassword
NICK alice
USER alice 0 * :Alice
JOIN #42
PRIVMSG #42 :hello there
```

Messages must be CR-LF terminated, so `nc` has to be told to send `\r\n`: that
is what `-C` does in GNU netcat and in nmap's `ncat`. The stock `nc` shipped
with macOS has no such option — use `ncat -C`, or simply a real IRC client.

## Usage examples

Registration is complete once `PASS`, `NICK` and `USER` have all been accepted;
the server then sends the welcome burst `001`–`004`. Commands sent before that
are answered with `451 ERR_NOTREGISTERED`, and a connection that never registers
is dropped after 60 seconds.

Creating a channel makes the creator its operator:

```
alice> JOIN #42
       :alice!alice@127.0.0.1 JOIN #42
       :ircserv 331 alice #42 :No topic is set
       :ircserv 353 alice = #42 :@alice
       :ircserv 366 alice #42 :End of /NAMES list
```

Operator commands:

```
alice> TOPIC #42 :C++98 only
alice> MODE #42 +itk secret     -> invite-only, topic locked to ops, key "secret"
alice> MODE #42 +l 10           -> at most 10 members
alice> MODE #42                 -> :ircserv 324 alice #42 +itkl secret 10
alice> INVITE bob #42
alice> MODE #42 +o bob          -> bob becomes channel operator
alice> KICK #42 bob :bye        -> bob is removed from the channel
```

Messaging:

```
alice> PRIVMSG #42 :message broadcast to every other member
alice> PRIVMSG bob :private message to a single user
alice> NOTICE #42 :like PRIVMSG, but never triggers an automatic reply
```

A channel is destroyed automatically when its last member leaves (`PART`,
`KICK`, `QUIT` or a lost connection).

## Features

### Commands

| Command | Registration required | Notes |
| --- | --- | --- |
| `PASS` | no | Connection password; `464` on mismatch, `462` once registered |
| `NICK` | no | 1–9 chars, RFC 2812 charset; `432` / `433` on invalid or taken |
| `USER` | no | `USER <user> <mode> <unused> :<realname>` |
| `CAP` | no | Minimal IRCv3 handshake (`LS` / `LIST` answered empty, `REQ` NAKed) so clients such as `irssi` proceed to registration |
| `QUIT` | no | Broadcasts the quit to every shared channel, then flushes and closes |
| `PING` | yes | Replies `PONG <server> :<token>` |
| `JOIN` | yes | Multi-target (`#a,#b key1,key2`); enforces `+i`, `+k`, `+l` |
| `PART` | yes | Multi-target, optional part message |
| `PRIVMSG` | yes | User and channel targets, multi-target with duplicate detection (`407`) |
| `NOTICE` | yes | Same routing as `PRIVMSG`, but never generates an error reply |
| `TOPIC` | yes | Shows the topic (`331` / `332`) or sets it; `+t` restricts setting to operators |
| `KICK` | yes | Operator only, multi-target, optional comment |
| `INVITE` | yes | Adds an invite exception for `+i`; replies `341` and notifies the invitee |
| `MODE` | yes | Channel modes below; `324` to query, `472` on unknown mode char |
| `WHO` | yes | Not required by the subject, implemented because reference clients query it after `JOIN` (`352` / `315`) |

Unknown commands are answered with `421 ERR_UNKNOWNCOMMAND`.

### Channel modes

| Mode | Parameter | Meaning |
| --- | --- | --- |
| `+i` / `-i` | — | Invite-only channel |
| `+t` / `-t` | — | Only channel operators may change the topic |
| `+k` / `-k` | key | Channel key (password), 1–23 printable ASCII characters |
| `+o` / `-o` | nick | Grant / revoke channel operator status |
| `+l` / `-l` | count | Member limit |

Modes may be combined (`MODE #42 +itk-l secret`); at most three
parameter-carrying modes are applied per command, and only the changes that
actually took effect are echoed back to the channel. `MODE #chan b` answers with
an empty ban list (`368`) so clients that probe it on join do not stall. User
modes are outside the subject and are ignored.

### Protocol handling

- **Message framing** — messages are CR-LF terminated and limited to 512 octets
  including CR-LF; a longer or malformed line gets an `ERROR :Closing Link`
  before the connection is closed.
- **Partial receives** — bytes are appended to a per-client input buffer and
  only parsed once a complete line has arrived, so `com` + `man` + `d\r\n` sent
  in three packets is handled as the single command `command`.
- **Non-blocking output** — replies are queued in a per-client output buffer and
  flushed on `POLLOUT`; a client that stops reading is disconnected once its
  queue exceeds 1 MiB instead of stalling the server.
- **Case mapping** — nicknames and channel names are compared with the RFC 1459
  rules (ASCII case folding plus `{}|^` ≡ `[]\~`).
- **Long replies** — outgoing lines are capped at 510 octets, and `353`
  (`RPL_NAMREPLY`) is split across as many lines as needed so no member is lost
  on a crowded channel.

## Technical choices

```
main ── Server::run()
         │  poll()  ← the only multiplexing point in the project
         │
         ├─ listen fd  POLLIN  ─▶ accept() (one per poll-ready event) + O_NONBLOCK
         ├─ client fd  POLLIN  ─▶ recv() ─▶ input buffer
         │                          └─ split on CR-LF ─▶ Message::parse
         │                               └─ CommandDispatcher ─▶ ACommand::execute
         │                                    └─ replies pushed to output buffer
         ├─ client fd  POLLOUT ─▶ send() as much as the socket accepts
         └─ stdout     POLLOUT ─▶ drain the log queue
```

- **One `poll()`, no fork, no threads.** Every descriptor — the listening
  socket, every client, and even `stdout` — is registered in the same poll set.
  `recv()` and `send()` are only ever called after `poll()` reported the
  corresponding event.
- **`stdout` in the poll set.** Logging goes to an in-memory queue that is
  drained in 512-byte chunks when `stdout` is writable, so a slow or blocked
  terminal can never block the event loop. If the queue fills up, lines are
  dropped and counted rather than stalling the server.
- **Command pattern.** Each command is one class deriving from `ACommand`,
  registered in `CommandDispatcher`. Validation failures throw `IrcException`,
  which the dispatcher formats into the matching numeric reply — so a command
  body reads as a straight list of guards.
- **Ownership.** `Server` owns clients and channels; `Client` holds only the
  channel names it joined, and `Channel` holds member pointers, so a disconnect
  is a single sweep with no dangling references.
- **Out-of-memory resilience.** Allocation points are exception-safe: a failed
  `new` during `accept()` drops that one connection, a `std::bad_alloc` inside
  the loop drops a client to reclaim memory, and the OOM warning path itself
  allocates nothing.
- **Non-blocking sockets** are set with `fcntl(fd, F_SETFL, O_NONBLOCK)` only,
  the single form the subject allows on macOS.

Source layout:

| Path | Contents |
| --- | --- |
| `src/main.cpp` | Argument validation and the top-level exception boundary |
| `src/Server.cpp`, `src/Socket.cpp` | Event loop, connection lifecycle, listening socket |
| `src/Client.cpp` | Per-client state and the input / output buffers |
| `src/Channel/` | Channel state: members, operators, invites, topic, modes, broadcast |
| `src/Message.cpp`, `src/Reply.cpp` | Message parsing and reply formatting |
| `src/commands/` | One file per IRC command |
| `src/CommandDispatcher.cpp` | Command table and dispatch |
| `src/Log.cpp` | Non-blocking structured logging |

## Testing

The project was developed on GitHub: every change went through a branch and a
pull request, and an automated GitHub Actions pipeline had to pass before it
could be merged. The pipeline ran, on every pull request:

- **Build matrix** — `make re` with both `g++` and `clang++` under
  `-Wall -Wextra -Werror -std=c++98 -pedantic-errors`
- **Unit and property tests** — message parsing, reply formatting, client
  buffers and channel state, compiled with AddressSanitizer and
  UndefinedBehaviorSanitizer
- **Allocation-failure tests** — every allocation point reached during
  construction and startup is forced to fail in turn, checking that the server
  rolls back cleanly instead of crashing or leaking
- **Fuzzing** — time-boxed libFuzzer runs against the byte-facing code (message
  parsing and line framing); any crash fails the build and the reproducer is
  uploaded as an artifact
- **End-to-end tests** — a sanitized build of the server driven over real
  sockets: full registration, every command, and commands delivered split across
  several packets
- **Coverage gate** — 100 % region, line, branch and MC/DC coverage required on
  the pure-logic files, plus an exhaustive sweep of the message parser; the
  coverage of the I/O and command layer is reported as an artifact
- **Static analysis** — `cppcheck` and `clang-tidy` (including the clang static
  analyzer), with warnings treated as errors

The test harness itself was a development tool and is not part of the server.

## Resources

- [RFC 1459 — Internet Relay Chat Protocol](https://datatracker.ietf.org/doc/html/rfc1459)
- [RFC 2812 — IRC Client Protocol](https://datatracker.ietf.org/doc/html/rfc2812) — message format and numeric replies
- [RFC 2811 — IRC Channel Management](https://datatracker.ietf.org/doc/html/rfc2811) — semantics of the `i`, `t`, `k`, `o`, `l` modes
- [Modern IRC Client Protocol](https://modern.ircdocs.horse/) — how current clients actually behave
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/) — sockets, `poll()`, non-blocking I/O
- `man 2 poll`, `man 2 socket`, `man 2 fcntl`

### Use of AI

AI was used for two supporting tasks only:

- **Translating the RFCs** (1459 / 2812 / 2811) into Japanese while reading them,
  to make sure the specification was understood correctly before implementing.
- **Researching IRC and `irssi` behaviour** — which numeric replies and message
  sequences a real reference client expects in practice, for cases the RFCs
  leave implicit (for example the `CAP` handshake sent on connect).

The design, the implementation and the tests were written by the authors.
