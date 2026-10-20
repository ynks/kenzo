#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace kenzo {

struct SessionInfo {
	std::filesystem::path path;
	std::chrono::system_clock::time_point start;
	std::uint64_t size = 0;
	std::chrono::seconds duration {0};
	bool crashed = false;
};

class SessionScanner {
public:
	using Post = std::function<void(std::function<void()>)>;
	using Done = std::function<void(std::vector<SessionInfo>)>;

	explicit SessionScanner(Post post) : post_(std::move(post)) {}
	~SessionScanner() { worker_.request_stop(); }

	void scan(Done done);

private:
	struct CacheEntry {
		std::uint64_t size = 0;
		std::filesystem::file_time_type mtime;
		SessionInfo info;
	};

	Post post_;
	std::mutex mutex_;
	std::map<std::filesystem::path, CacheEntry> cache_;
	std::jthread worker_;
};

}  // namespace kenzo
