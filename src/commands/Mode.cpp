#include "commands/Mode.hpp"

#include <climits>
#include <sstream>
#include <string>
#include <vector>

#include "Channel.hpp"
#include "Client.hpp"
#include "IrcException.hpp"
#include "Log.hpp"
#include "Message.hpp"
#include "Reply.hpp"
#include "Server.hpp"
#include "StringUtil.hpp"

namespace {
	// 適用できた変更(フラグ列と対応する引数)を貯めて broadcast 文字列の材料にする
	struct ModeChanges {
		std::string flags;
		char        lastSign;
		std::vector<std::string> args;
		ModeChanges() :
			flags(),
			lastSign('\0'),
			args()
		{
		}

		bool any() const {
			return !flags.empty();
		}
	};

	// 数値応答(:server <code> <nick> <detail>)を1行送るヘルパ
	void sendNumeric(Server& server, Client& client, int code, const std::string& detail) {
		server.sendLine(client, Reply::numeric(server.serverName(), code, client.nick(), detail));
	}

	// 適用したフラグを符号込みで flags に追記(同符号が続くときは符号を重複させない)
	void record(ModeChanges& c, char sign, char flag) {
		if (c.lastSign != sign) {
			c.flags += sign;
			c.lastSign = sign;
		}
		c.flags += flag;
	}

	//"+l"設定時に有効な人数上限として数値変換する
	bool parseLimit(const std::string& s, std::size_t& out) {
		if (s.empty()) {
			return false;
		}
		for (std::string::size_type i = 0; i < s.size(); ++i) {
			if (s[i] < '0' || s[i] > '9') {
				return false;
			}
		}
		std::istringstream iss(s);
		unsigned long v = 0;
		iss >> v;
		if (iss.fail() || !iss.eof()) {
			return false;
		}
		if (v == 0) {
			return false;
		}
		if (v > static_cast<unsigned long>(LONG_MAX)) {   // long キャストで負数化するのを防ぐ
			return false;
		}
		out = static_cast<std::size_t>(v);
		return true;
	}

	// チャンネル鍵が RFC2812 §2.3.1 の文法(+運用上のローカル制限)に合うか検証する
	bool isValidKey(const std::string& key) {
		if (key.empty() == true || key.size() > 23)
			return false;
		// ローカル制限(ABNF超・運用安全): 先頭コロンは MODE broadcast の trailing 解釈で、鍵が desync するため拒否(途中コロンは許可)。
		if (key[0] == ':')
			return false;
		// RFC2812 §2.3.1 の許可集合を「除外リスト」で表現(等価だが可読)。
		// 許可: 7-bit US-ASCII から、下の制御文字群・空白・カンマ・先頭コロンを除いたもの。
		for (std::string::size_type i = 0; i < key.size(); ++i) {
			unsigned char uc = static_cast<unsigned char>(key[i]);
			// 7-bit US-ASCII 以外(NUL と 0x80 以上の非ASCII)を拒否。
			if (uc == '\0' || uc > 0x7F)
				return false;
			// ABNF が除外する制御文字・空白を拒否。ACK(0x06) だけは対応する文字リテラルが
			// 無いため 16 進(Errata 4836: コメントは FF も除外と書くが ABNF 本体=FF許可を採用)。
			if (uc == '\t' || uc == '\n' || uc == '\v' || uc == '\r' || uc == ' ' || uc == 0x06)
				return false;
			// ローカル制限(ABNF超・運用安全): カンマは JOIN の鍵リスト分割と衝突するため拒否。
			if (uc == ',')
				return false;
		}
		return true;
	}

	// 引数文字列だけで判定できる値検証の関数ポインタ型(k=鍵 / l=上限 が使う)
	typedef bool (*ModeValidateFn)(char sign, const std::string& arg);

	// 鍵の文字列検証(RFC2812 §2.3.1)。-k は鍵内容を検証しない(値検証は k と l が持つ)。
	bool validateKey(char sign, const std::string& arg) {
		if (sign == '-')
			return true;
		return isValidKey(arg);
	}

	// +l の値検証をテーブルへ寄せる（+l のみ argOnSet なので sign は常に '+'）。
	bool validateLimit(char sign, const std::string& arg) {
		(void)sign;
		std::size_t dummy;
		return parseLimit(arg, dummy);
	}

	// 1モードフラグの消費規則(引数要否・値検証・失敗時numeric)を表すテーブル1行
	// ※フィールドは padding 最小化のため型の大きい順に並べる(clang-tidy optin.performance.Padding)
	struct ModeSpec {
		ModeValidateFn validate;     // 文字列だけの値検証(不要なら 0)。k/l が非0
		const char*    badArgDetail; // validate 失敗時メッセージ(":..." 形式)
		int            badArgReply;  // validate 失敗時 numeric
		char           flag;
		bool           argOnSet;     // +flag が1引数を取るか
		bool           argOnUnset;   // -flag が1引数を取るか
	};

	// モード消費規則の唯一の真実源(i,t=引数なし / k,l=+のみ引数 / o=+-両引数)
	// -k は引数を取らない: irssi の /mode #c -k は引数なしで送られ、461 を返すと
	// 鍵が外せなくなる(subject 必須の「k: Set/remove the channel key」)。
	// 解除の通知には applyOne が解除前の実鍵を echo する。
	static const ModeSpec kModeTable[] = {
		// validate        badArgDetail            badArgReply                flag argOnSet argOnUnset
		{ 0,              0,                      0,                         'i', false,   false },
		{ 0,              0,                      0,                         't', false,   false },
		{ &validateKey,   ":Invalid channel key", Reply::ERR_NEEDMOREPARAMS, 'k', true,    false },
		{ &validateLimit, 0,                      0,                         'l', true,    false }, // badArgReply=0 → 値不正は無音スキップ
		{ 0,              0,                      0,                         'o', true,    true  },
	};
	static const std::size_t kModeTableSize = sizeof(kModeTable) / sizeof(kModeTable[0]);

	static const std::size_t MAX_PARAM_MODES = 3;   // RFC2812 §3.2.3: 引数付きモードは1コマンド最大3件

	// フラグ文字からテーブル行を線形探索して返す(未知フラグは 0)
	const ModeSpec* findSpec(char flag) {
		for (std::size_t i = 0; i < kModeTableSize; ++i)
			if (kModeTable[i].flag == flag)
				return &kModeTable[i];
		return 0;   // 未知フラグ
	}

	// そのモードがこの符号(+/-)で引数を1つ消費するかをテーブルから判定する
	bool consumesArg(const ModeSpec* spec, char sign) {
		if (spec == 0)
			return false;                     // 未知フラグは引数を消費しない
		if (sign == '+')
			return spec->argOnSet;
		return spec->argOnUnset;
	}

	// nextModeStep が1文字を解釈した結果(符号か / spec / 引数消費有無 / 取り出した引数 / 引数不足)
	struct ModeStep {
		bool            isSign;
		const ModeSpec* spec;
		bool            takesArg;
		std::string     arg;
		bool            argMissing;
	};

	// argIdx を前進させる唯一の関数。消費規則(テーブル)に従い前進を1箇所に集約する。
	ModeStep nextModeStep(char c, char& sign, const Message& msg, std::size_t& argIdx) {
		ModeStep st;
		st.isSign = false;
		st.spec = 0;
		st.takesArg = false;
		st.argMissing = false;
		if (c == '+' || c == '-') {
			sign = c;
			st.isSign = true;
			return st;
		}
		st.spec = findSpec(c);
		st.takesArg = consumesArg(st.spec, sign);
		if (st.takesArg == true) {
			if (argIdx >= msg.size())
				st.argMissing = true;      // 461条件。消費も前進もしない
			else {
				st.arg = msg.param(argIdx);
				++argIdx;
			}        // argIdx 前進はこの1行のみ
		}
		return st;
	}

	// 検証済みの1モードを実際にチャンネル状態へ適用し changes に記録する(引数消費はしない)
	void applyOne(Server& server, Client& client, Channel& channel,
			char sign, char flag, const std::string& arg, ModeChanges& changes) {
		switch (flag) {
		case 'i':
			if (sign == '+' && channel.inviteOnly() == false) {
				channel.setInviteOnly(true);
				record(changes, '+', 'i');
			} else if (sign == '-' && channel.inviteOnly() == true) {
				channel.setInviteOnly(false);
				record(changes, '-', 'i');
			}
			return;
		case 't':
			if (sign == '+' && channel.topicLocked() == false) {
				channel.setTopicLocked(true);
				record(changes, '+', 't');
			} else if (sign == '-' && channel.topicLocked() == true) {
				channel.setTopicLocked(false);
				record(changes, '-', 't');
			}
			return;
		case 'k':
			if (sign == '+') {
				if (channel.hasKey() == false || channel.key() != arg) {
					channel.setKey(arg);
					record(changes, '+', 'k');
					changes.args.push_back(arg);
				}
			} else {  // -k : arg は使わない。通知には解除前の実鍵をechoする。
				if (channel.hasKey() == true) {
					changes.args.push_back(channel.key());   // 解除前の実鍵（検証済みで安全）
					channel.clearKey();
					record(changes, '-', 'k');
				}
				// key未設定なら no-op（record も push もしない）
			}
			return;
		case 'l':
			if (sign == '+') {
				std::size_t n;
				if (parseLimit(arg, n) == false) {
					return;
				}
				if (channel.hasLimit() == false || channel.limit() != n) {
					channel.setLimit(n);
					record(changes, '+', 'l');
					changes.args.push_back(
							StringUtil::toString(static_cast<long>(n)));
				}
			} else {
				if (channel.hasLimit() == true) {
					channel.clearLimit();
					record(changes, '-', 'l');
				}
			}
			return;
		case 'o': {
			Client* tgt = server.findClientByNick(arg);
			if (tgt == 0 || channel.hasMember(*tgt) == false) {
				sendNumeric(server, client, Reply::ERR_USERNOTINCHANNEL,
						arg + " " + channel.name()
						+ " :They aren't on that channel");
				return;
			}
			if (sign == '+') {
				if (channel.isOperator(*tgt) == false) {
					channel.addOperator(*tgt);
					record(changes, '+', 'o');
					changes.args.push_back(arg);
				}
			} else {
				if (channel.isOperator(*tgt) == true) {
					channel.removeOperator(*tgt);
					record(changes, '-', 'o');
					changes.args.push_back(arg);
				}
			}
			return;
		}
		default:
			return;   // 未知フラグは applyModes 側で472を送る。ここへは到達しない前提。
		}
	}

	// 真の1パス・ベストエフォート。左から (sign, flag, arg) を1回だけ走査し検証・エラー送出・適用を統合。
	// エラーは throw せず sendNumeric で出現順に送る。部分適用を許容(実IRCのベストエフォート)。
	void applyModes(Server& server, Client& client, Channel& channel,
			const std::string& modestr, const Message& msg, ModeChanges& changes) {
		std::size_t argIdx = 2;
		char sign = '+';
		std::size_t paramModeCount = 0;      // RFC2812 §3.2.3: 引数を消費したモードの件数
		bool sentNeedMoreParams = false;     // 引数不足461を1コマンド1回に抑制(不正鍵461は別経路)
		for (std::string::size_type i = 0; i < modestr.size(); ++i) {
			ModeStep st = nextModeStep(modestr[i], sign, msg, argIdx);
			if (st.isSign == true)
				continue;
			if (st.spec == 0) {
				sendNumeric(server, client, Reply::ERR_UNKNOWNMODE,
						std::string(1, modestr[i])
						+ " :is unknown mode char to me for " + channel.name());
				continue;
			}
			if (st.takesArg == true) {
				if (paramModeCount >= MAX_PARAM_MODES)
					continue;
				if (st.argMissing == true) {
					if (sentNeedMoreParams == false) {
						sendNumeric(server, client, Reply::ERR_NEEDMOREPARAMS,
								"MODE :Not enough parameters");
						sentNeedMoreParams = true;
					}
					continue;
				}
				++paramModeCount;
			}
			if (st.takesArg == true && st.spec->validate != 0 && st.spec->validate(sign, st.arg) == false) {
				if (st.spec->badArgReply != 0)
					sendNumeric(server, client, st.spec->badArgReply,
							std::string("MODE ") + st.spec->badArgDetail);
				continue;
			}
			applyOne(server, client, channel, sign, modestr[i], st.arg, changes);
		}
	}

	// changes からブロードキャスト用のモード文字列(例 "+kl secret 10")を組み立てる
	std::string buildAppliedString(const ModeChanges& c) {
		std::string result = c.flags;
		for (std::size_t i = 0; i < c.args.size(); ++i) {
			result += " " + c.args[i];
		}
		return result;
	}
}

// MODE は登録完了後のみ許可(未登録での実行は Dispatcher が 451 で弾く)
bool ModeCommand::needsRegistration() const {
	return true;
}

// MODE コマンド入口。引数なしはクエリ(324)、ありは applyModes で適用し変更を broadcast する
void ModeCommand::execute(Server& server, Client& client, const Message& msg) {
	if (msg.size() == 0 || msg.param(0).empty() == true) {
		throw IrcException(Reply::ERR_NEEDMOREPARAMS, client.nick(),
				"MODE :Not enough parameters");
	}
	const std::string& target = msg.param(0);
	if (target[0] != '#') {
		return;
	}
	Channel* channel = server.findChannel(target);
	if (channel == 0) {
		throw IrcException(Reply::ERR_NOSUCHCHANNEL, client.nick(),
				target + " :No such channel");
	}
	if (msg.size() < 2) {
		sendNumeric(server, client, Reply::RPL_CHANNELMODEIS,
				channel->name() + " " + channel->modeString(client));
		return;
	}
	// RFC2812 §3.2.3: 引数なしの +b はバンリストの「照会」なので非opにも許す。
	// irssi は join 直後に MODE <chan> b を自動送出するので、482 を返すと
	// チャンネル窓にエラーが出る。+b は未実装なので常に空リストを返す。
	if (msg.size() == 2 && (msg.param(1) == "b" || msg.param(1) == "+b")) {
		sendNumeric(server, client, Reply::RPL_ENDOFBANLIST,
				channel->name() + " :End of channel ban list");
		return;
	}
	if (channel->isOperator(client) == false) {
		throw IrcException(Reply::ERR_CHANOPRIVSNEEDED, client.nick(),
				channel->name() + " :You're not channel operator");
	}
	const std::string& modestr = msg.param(1);
	ModeChanges changes;
	applyModes(server, client, *channel, modestr, msg, changes);
	if (changes.any() == true) {
		const std::string applied = buildAppliedString(changes);
		channel->broadcast(Reply::from(client.prefix(),
				"MODE " + channel->name() + " " + applied));
		Log::mode("@ " + client.nick() + " set " + channel->name() + " "
				+ applied);
	}
}
