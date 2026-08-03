CXX      := clang++
CXXFLAGS := -Wall -Wextra -Werror -std=c++98 -O2 -g -I ../include -I .
SAN      := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
COV      := -fprofile-instr-generate -fcoverage-mapping
MCDC     := -fcoverage-mcdc
FUZZSAN  := -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer

SRCS := verify.cpp \
        ../src/Message.cpp ../src/Reply.cpp ../src/StringUtil.cpp \
        ../src/Client.cpp ../src/IrcException.cpp \
        ../src/Channel/Channel.cpp ../src/Channel/Channel_members.cpp \
        ../src/Channel/Channel_operator.cpp ../src/Channel/Channel_invite.cpp \
        ../src/Channel/Channel_topic.cpp ../src/Channel/Channel_mode.cpp \
        ../src/Channel/Channel_broadcast.cpp

FUZZ_SRCS := fuzz.cpp ../src/Message.cpp ../src/Client.cpp

SERVER_SRCS := $(sort $(wildcard ../src/*.cpp) $(wildcard ../src/*/*.cpp))
SERVER_LIB_SRCS := $(filter-out ../src/main.cpp,$(SERVER_SRCS))

verify: $(SRCS) spec_parser.hpp
	$(CXX) $(CXXFLAGS) $(SAN) $(SRCS) -o $@

oom: oom.cpp $(SERVER_LIB_SRCS)
	$(CXX) $(CXXFLAGS) $(SAN) -I ../include/commands oom.cpp $(SERVER_LIB_SRCS) -o $@

ircserv_asan: $(SERVER_SRCS)
	$(CXX) $(CXXFLAGS) $(SAN) -I ../include/commands $(SERVER_SRCS) -o $@

verify_cov: $(SRCS) spec_parser.hpp
	$(CXX) $(CXXFLAGS) $(COV) $(MCDC) $(SRCS) -o $@

ircserv_cov: $(SERVER_SRCS)
	$(CXX) $(CXXFLAGS) $(COV) -I ../include/commands $(SERVER_SRCS) -o $@

fuzz: $(FUZZ_SRCS)
	$(CXX) $(CXXFLAGS) $(FUZZSAN) $(FUZZ_SRCS) -o $@

clean:
	rm -f verify oom verify_cov ircserv_cov fuzz ircserv_asan *.profraw *.profdata cov*.json

.PHONY: clean
