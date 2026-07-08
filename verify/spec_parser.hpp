#ifndef SPEC_PARSER_HPP
#define SPEC_PARSER_HPP

// 実行可能仕様（executable specification）
//
// SPEC.md の文法をそのまま有限状態機械として転写したもの。
// src/Message.cpp（インデックス走査＋substr方式）とは意図的に
// 異なる構造（1文字ずつのFSM）で書き、両者の全数一致をもって
// 実装が仕様を満たす証拠とする（N-version差分検査）。

#include <cctype>
#include <string>
#include <vector>

struct SpecMessage {
	std::string              prefix;
	std::string              command;
	std::vector<std::string> params;
	bool                     isEmpty;
};

inline SpecMessage specParse(const std::string& line) {
	enum State {
		S_LEAD,      // 行頭の空白
		S_PREFIX,    // ':' に続く prefix 本体
		S_CMDSEP,    // prefix とコマンドの間の空白
		S_CMD,       // コマンド本体
		S_SEP,       // パラメータ間の空白
		S_MIDDLE,    // 通常パラメータ本体
		S_TRAILING   // ':' 以降の trailing（行末まで丸ごと）
	};

	SpecMessage m;
	m.isEmpty = true;

	State st = S_LEAD;
	std::string cur;

	for (std::string::size_type i = 0; i < line.size(); ++i) {
		char c = line[i];
		switch (st) {
		case S_LEAD:
			if (c == ' ') break;
			if (c == ':') { st = S_PREFIX; break; }
			cur += c;
			st = S_CMD;
			break;
		case S_PREFIX:
			if (c == ' ') { m.prefix = cur; cur.clear(); st = S_CMDSEP; break; }
			cur += c;
			break;
		case S_CMDSEP:
			if (c == ' ') break;
			cur += c;
			st = S_CMD;
			break;
		case S_CMD:
			if (c == ' ') { m.command = cur; cur.clear(); st = S_SEP; break; }
			cur += c;
			break;
		case S_SEP:
			if (c == ' ') break;
			if (c == ':') { st = S_TRAILING; break; }
			cur += c;
			st = S_MIDDLE;
			break;
		case S_MIDDLE:
			if (c == ' ') { m.params.push_back(cur); cur.clear(); st = S_SEP; break; }
			cur += c;
			break;
		case S_TRAILING:
			cur += c;
			break;
		}
	}

	switch (st) {
	case S_LEAD:
	case S_CMDSEP:
	case S_SEP:
		break;
	case S_PREFIX:
		m.prefix = cur;  // prefixのみの行：コマンド無し＝空メッセージ
		break;
	case S_CMD:
		m.command = cur;
		break;
	case S_MIDDLE:
	case S_TRAILING:
		m.params.push_back(cur);  // 空のtrailing（"CMD :"）も1パラメータ
		break;
	}

	for (std::string::size_type i = 0; i < m.command.size(); ++i) {
		m.command[i] = static_cast<char>(
				std::toupper(static_cast<unsigned char>(m.command[i])));
	}
	m.isEmpty = m.command.empty();
	return m;
}

#endif
