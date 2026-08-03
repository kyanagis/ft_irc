#ifndef LOG_HPP
#define LOG_HPP

#include <cstddef>
#include <string>

class Client;

class Log
{
public:
	static void        reserve();
	static bool        hasPending();
	static std::size_t drainOnce();
	static unsigned long droppedLines();

	static void banner(const std::string& serverName, const std::string& version);
	static void field(const std::string& key, const std::string& value);
	static void rule();

	static void info(const std::string& text);
	static void warn(const std::string& text);
	static void err(const std::string& text);
	static void conn(const std::string& text);
	static void auth(const std::string& text);
	static void chan(const std::string& text);
	static void memb(const std::string& text);
	static void mode(const std::string& text);
	static void deny(const std::string& text);

	static void relay(const std::string& text);
	static void trace(const std::string& text);

	static bool traceEnabled();

	static void oomWarn(const char* what, const char* detail = 0,
			const char* extra = 0);

	static std::string who(const Client& client);

private:
	Log();
	Log(const Log&);
	Log& operator=(const Log&);
};

#endif
