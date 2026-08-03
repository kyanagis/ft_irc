#include <stdint.h>
#include <cstddef>
#include <string>

#include "Client.hpp"
#include "Message.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
	const char* bytes = reinterpret_cast<const char*>(data);
	std::string s(bytes, size);

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
	(void)m.param(m.size() + 5);

	Client c(-1, "fuzz");
	c.appendInput(bytes, size);
	std::string line;
	while (c.extractLine(line)) {
	}

	return 0;
}
