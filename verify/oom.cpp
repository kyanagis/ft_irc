// Server/CommandDispatcherの構築中に各newを順番に失敗させ、C++例外巻き戻し後に
// テスト中の生存allocationが残らないことを決定的に検査する。

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>

#include "Server.hpp"

namespace {
	union AllocationHeader {
		long double alignment;
		struct {
			std::size_t magic;
			bool tracked;
		} meta;
	};

	const std::size_t HEADER_MAGIC = 0x495243UL;
	bool              g_track = false;
	std::size_t       g_calls = 0;
	std::size_t       g_failAt = 0;
	std::size_t       g_live = 0;

	void* allocate(std::size_t size) {
		if (g_track) {
			++g_calls;
			if (g_failAt != 0 && g_calls == g_failAt) {
				throw std::bad_alloc();
			}
		}
		if (size > static_cast<std::size_t>(-1) - sizeof(AllocationHeader)) {
			throw std::bad_alloc();
		}
		void* raw = std::malloc(sizeof(AllocationHeader) + size);
		if (raw == 0) {
			throw std::bad_alloc();
		}
		AllocationHeader* header = static_cast<AllocationHeader*>(raw);
		header->meta.magic = HEADER_MAGIC;
		header->meta.tracked = g_track;
		if (g_track) {
			++g_live;
		}
		return header + 1;
	}

	void deallocate(void* ptr) {
		if (ptr == 0) {
			return;
		}
		AllocationHeader* header =
				static_cast<AllocationHeader*>(ptr) - 1;
		if (header->meta.magic != HEADER_MAGIC) {
			std::abort();
		}
		if (header->meta.tracked) {
			--g_live;
		}
		header->meta.magic = 0;
		std::free(header);
	}

	bool constructServer(std::size_t failAt, std::size_t& calls,
			std::size_t& live) {
		g_calls = 0;
		g_failAt = failAt;
		g_live = 0;
		g_track = true;
		try {
			const std::string password("pass");
			Server server(6667, password);
		}
		catch (const std::bad_alloc&) {
		}
		catch (...) {
			g_track = false;
			return false;
		}
		g_track = false;
		calls = g_calls;
		live = g_live;
		return true;
	}
}

void* operator new(std::size_t size) throw(std::bad_alloc) {
	return allocate(size);
}

void* operator new[](std::size_t size) throw(std::bad_alloc) {
	return allocate(size);
}

void operator delete(void* ptr) throw() {
	deallocate(ptr);
}

void operator delete[](void* ptr) throw() {
	deallocate(ptr);
}

int main() {
	std::size_t totalCalls = 0;
	std::size_t live = 0;
	if (!constructServer(0, totalCalls, live) || live != 0) {
		std::fprintf(stderr, "OOM baseline construction leaked or threw\n");
		return 1;
	}

	for (std::size_t failAt = 1; failAt <= totalCalls + 1; ++failAt) {
		std::size_t calls = 0;
		if (!constructServer(failAt, calls, live) || live != 0) {
			std::fprintf(stderr,
					"OOM failure at allocation %lu left %lu allocations\n",
					static_cast<unsigned long>(failAt),
					static_cast<unsigned long>(live));
			return 1;
		}
	}

	std::printf("OOM construction rollback: %lu allocation points ok\n",
			static_cast<unsigned long>(totalCalls));
	return 0;
}
