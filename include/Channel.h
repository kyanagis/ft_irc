#ifndef CHANNEL_H
#define CHANNEL_H

#include <string>
#include <set>
#include <cstddef>

class Client;

class Channel
{
public:
	Channel(const std::string& name, Client& creator);

	const std::string& name() const;

	const std::string& topic() const;
	const std::string& topicSetter() const;
	bool               hasTopic() const;
	void               setTopic(const std::string& topic, const std::string& setterNick);

	void                     addMember(Client& client);
	void                     removeMember(Client& client);
	bool                     hasMember(Client& client) const;
	bool                     isEmpty() const;
	std::size_t              memberCount() const;
	const std::set<Client*>& members() const;
	void                     broadcast(const std::string& message, Client* except = 0);

	bool isOperator(Client& client) const;
	void addOperator(Client& client);
	void removeOperator(Client& client);

	void invite(Client& client);
	bool isInvited(Client& client) const;
	void clearInvite(Client& client);

	bool inviteOnly() const;   void setInviteOnly(bool on);
	bool topicLocked() const;  void setTopicLocked(bool on);
	bool hasKey() const;       const std::string& key() const;
	void setKey(const std::string& key);  void clearKey();
	bool hasLimit() const;     std::size_t limit() const;
	void setLimit(std::size_t limit);  void clearLimit();

	std::string modeString() const;

private:
	Channel(const Channel&);
	Channel& operator=(const Channel&);

	std::string       _name;
	std::string       _topic;
	std::string       _topicSetter;
	bool              _hasTopic;
	std::set<Client*> _members;
	std::set<Client*> _operators;
	std::set<Client*> _invited;
	bool              _inviteOnly;
	bool              _topicLocked;
	bool              _hasKey;
	std::string       _key;
	bool              _hasLimit;
	std::size_t       _limit;
};

#endif
