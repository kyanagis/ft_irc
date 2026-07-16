# 検証ハーネス
# ASan/UBSanとカバレッジ計測のためApple clang++を明示
CXX      := clang++
CXXFLAGS := -Wall -Wextra -Werror -std=c++98 -O2 -g -I ../include -I .
# -fno-sanitize-recover=all: UBSan を「表示して継続」ではなく即 abort させ、
# env(UBSAN_OPTIONS) に依存せず verify/fuzz 双方でUBを確実に赤にする
SAN      := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
COV      := -fprofile-instr-generate -fcoverage-mapping
FUZZSAN  := -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer

# 検証対象のコード（
SRCS := verify.cpp \
        ../src/Message.cpp ../src/Reply.cpp ../src/StringUtil.cpp \
        ../src/Client.cpp ../src/IrcException.cpp \
        ../src/Channel/Channel.cpp ../src/Channel/Channel_members.cpp \
        ../src/Channel/Channel_operator.cpp ../src/Channel/Channel_invite.cpp \
        ../src/Channel/Channel_topic.cpp ../src/Channel/Channel_mode.cpp \
        ../src/Channel/Channel_broadcast.cpp

FUZZ_SRCS := fuzz.cpp ../src/Message.cpp ../src/Client.cpp

SERVER_SRCS := $(sort $(wildcard ../src/*.cpp) $(wildcard ../src/*/*.cpp))

verify: $(SRCS) spec_parser.hpp
	$(CXX) $(CXXFLAGS) $(SAN) $(SRCS) -o $@

# ../include/commands も要る（コマンド実装のヘッダ）
ircserv_asan: $(SERVER_SRCS)
	$(CXX) $(CXXFLAGS) $(SAN) -I ../include/commands $(SERVER_SRCS) -o $@

verify_cov: $(SRCS) spec_parser.hpp
	$(CXX) $(CXXFLAGS) $(COV) $(SRCS) -o $@

# バイトパーサのファジング（libFuzzer）。CI で time-box 実行する
fuzz: $(FUZZ_SRCS)
	$(CXX) $(CXXFLAGS) $(FUZZSAN) $(FUZZ_SRCS) -o $@

clean:
	rm -f verify verify_cov fuzz ircserv_asan *.profraw *.profdata

.PHONY: clean
