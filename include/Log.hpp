#ifndef LOG_HPP
#define LOG_HPP

#include <string>

class Client;

// サーバ側のターミナル出力（IRCプロトコルとは無関係・stdoutへ1行ずつflush）．
// 環境変数: NO_COLOR / TERM=dumb で色を落とす．IRC_TRACE=1 で受理コマンドの生トレースも出す．
class Log
{
public:
	// 起動バナー: アスキーアート＋見出し．field/rule で情報パネルを続けて書く
	static void banner(const std::string& serverName, const std::string& version);
	static void field(const std::string& key, const std::string& value);
	static void rule();

	static void info(const std::string& text);
	static void warn(const std::string& text);
	static void err(const std::string& text);
	static void conn(const std::string& text);    // 接続・切断
	static void auth(const std::string& text);    // 登録・改名
	static void chan(const std::string& text);    // チャンネル生成・破棄
	static void memb(const std::string& text);    // JOIN/PART/KICK/INVITE
	static void mode(const std::string& text);    // MODE/TOPIC
	static void relay(const std::string& text);   // PRIVMSG/NOTICE の中継
	static void deny(const std::string& text);    // エラー数値応答で拒否した操作
	static void trace(const std::string& text);   // IRC_TRACE=1 のときだけ出る

	static bool traceEnabled();

	// ログ用のクライアント表記．nick未確定なら *@host
	static std::string who(const Client& client);

private:
	Log();
	Log(const Log&);
	Log& operator=(const Log&);
};

#endif
