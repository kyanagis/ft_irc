#ifndef REPLY_H
#define REPLY_H

#include <string>

class Reply
{
public:
	static const int RPL_WELCOME          = 1;
	static const int RPL_YOURHOST         = 2;
	static const int RPL_CREATED          = 3;
	static const int RPL_MYINFO           = 4;
	static const int RPL_CHANNELMODEIS    = 324;
	static const int RPL_NOTOPIC          = 331;
	static const int RPL_TOPIC            = 332;
	static const int RPL_INVITING         = 341;
	static const int RPL_NAMREPLY         = 353;
	static const int RPL_ENDOFNAMES       = 366;
	static const int ERR_NOSUCHNICK       = 401;
	static const int ERR_NOSUCHCHANNEL    = 403;
	static const int ERR_CANNOTSENDTOCHAN = 404;
	static const int ERR_NORECIPIENT      = 411;
	static const int ERR_NOTEXTTOSEND     = 412;
	static const int ERR_UNKNOWNCOMMAND   = 421;
	static const int ERR_NONICKNAMEGIVEN  = 431;
	static const int ERR_ERRONEUSNICKNAME = 432;
	static const int ERR_NICKNAMEINUSE    = 433;
	static const int ERR_USERNOTINCHANNEL = 441;
	static const int ERR_NOTONCHANNEL     = 442;
	static const int ERR_USERONCHANNEL    = 443;
	static const int ERR_NOTREGISTERED    = 451;
	static const int ERR_NEEDMOREPARAMS   = 461;
	static const int ERR_ALREADYREGISTRED = 462;
	static const int ERR_PASSWDMISMATCH   = 464;
	static const int ERR_CHANNELISFULL    = 471;
	static const int ERR_UNKNOWNMODE      = 472;
	static const int ERR_INVITEONLYCHAN   = 473;
	static const int ERR_BADCHANNELKEY    = 475;
	static const int ERR_CHANOPRIVSNEEDED = 482;

	static std::string numeric(const std::string& server, int code,
	                           const std::string& target, const std::string& rest);

	static std::string from(const std::string& prefix, const std::string& body);

private:
	Reply();
	Reply(const Reply&);
	Reply& operator=(const Reply&);
};

#endif
