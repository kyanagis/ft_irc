#include <cctype>
#include <cstdlib>

#include <exception>
#include <iostream>
#include <new>
#include <string>

#include "Server.hpp"

namespace {
	bool parsePort(const std::string& s, int& out) {
		if (s.empty()) {
			return false;
		}
		for (std::size_t i = 0; i < s.size(); ++i) {
			if (!std::isdigit(static_cast<unsigned char>(s[i]))) {
				return false;
			}
		}
		long value = std::strtol(s.c_str(), 0, 10);
		if (value < 1 || value > 65535) {
			return false;
		}
		out = static_cast<int>(value);
		return true;
	}
}

int main(int argc, char** argv) {
	try {
		if (argc != 3) {
			std::cerr << "Usage: " << argv[0] << " <port> <password>\n";
			return 1;
		}

		int port;
		if (!parsePort(argv[1], port)) {
			std::cerr << "Error: port must be an integer in [1, 65535]\n";
			return 1;
		}

		std::string password = argv[2];
		if (password.empty()) {
			std::cerr << "Error: password must not be empty\n";
			return 1;
		}

		Server server(port, password);
		server.run();
	}
	catch (const std::bad_alloc&) {
		return 1;
	}
	catch (const std::exception& e) {
		std::cerr << "Fatal: " << e.what() << '\n';
		return 1;
	}
	return 0;
}
