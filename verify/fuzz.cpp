// libFuzzer ターゲット：バイト境界の実コードを任意入力で叩き、落ちないことを検査する
//
// 検証内容:
//   - Message::parse … 任意バイト列で crash / UB なし（ASan/UBSan 下）＋構造不変条件
//   - Client::extractLine … 任意バイトを流し込んでも停止・完走し、範囲外アクセスなし
//   - N8（絶対にクラッシュしない）を生入力ファジングで直接ハードニングする
//
//   ビルド: clang++ -std=c++98 -fsanitize=fuzzer,address,undefined ...
//   実行  : ./fuzz -max_total_time=<秒>   （crash 検出で非0終了＝CI失敗）

#include <stdint.h>
#include <cstddef>
#include <string>

#include "Client.hpp"
#include "Message.hpp"

// - libFuzzer が1入力ごとに呼ぶエントリ
// - パーサの不変条件違反は __builtin_trap で即失敗にする
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
	const char* bytes = reinterpret_cast<const char*>(data);
	std::string s(bytes, size);

	// 1) パーサ：落ちない＋「empty⇔command空」「commandに空白なし」「範囲外は空」
	Message m = Message::parse(s);
	if (m.empty() != m.command().empty()) {
		__builtin_trap();
	}
	const std::string& cmd = m.command();
	for (std::size_t i = 0; i < cmd.size(); ++i) {
		if (cmd[i] == ' ') {
			__builtin_trap();
		}
	}
	(void)m.param(m.size() + 5);  // 範囲外アクセスが安全（空文字列）であること

	// 2) 行分割：部分受信の再構築が任意バイトで停止・完走すること（fd=-1はI/Oしない）
	Client c(-1, "fuzz");
	c.appendInput(bytes, size);
	std::string line;
	while (c.extractLine(line)) {
		// 取り出せる限り消費するだけ
	}

	return 0;
}
