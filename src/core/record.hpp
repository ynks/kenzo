#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kenzo {

enum Sev : std::uint8_t { kTrace, kInfo, kWarning, kError, kCritical };
inline constexpr int kSevCount = 5;

using StrId = std::uint32_t;

class Interner {
public:
	Interner() { intern(""); }

	StrId intern(std::string_view text) {
		{
			std::shared_lock lock(mutex_);
			if (auto it = map_.find(text); it != map_.end()) {
				return it->second;
			}
		}
		std::unique_lock lock(mutex_);
		if (auto it = map_.find(text); it != map_.end()) {
			return it->second;
		}
		const auto id = static_cast<StrId>(strings_.size());
		strings_.emplace_back(text);
		map_.emplace(std::string_view(strings_.back()), id);
		return id;
	}

	[[nodiscard]] std::string_view get(StrId id) const {
		std::shared_lock lock(mutex_);
		return strings_[id];
	}

	[[nodiscard]] std::size_t size() const {
		std::shared_lock lock(mutex_);
		return strings_.size();
	}

private:
	mutable std::shared_mutex mutex_;
	std::deque<std::string> strings_;
	std::unordered_map<std::string_view, StrId> map_;
};

struct RawRecord {
	std::uint64_t ts = 0;
	std::uint32_t line = 0;
	std::uint8_t sev = kInfo;
	std::string sink, file, message, details, backtrace;
};

struct LogRecord {
	std::uint64_t ts = 0;
	std::uint32_t line = 0;
	StrId sink = 0, file = 0, backtrace = 0;
	std::uint8_t sev = kInfo;
	std::string message, details;
};

}  // namespace kenzo
