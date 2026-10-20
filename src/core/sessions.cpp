#include "core/sessions.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "core/csv.hpp"
#include "core/paths.hpp"
#include "core/query.hpp"

namespace kenzo {
namespace {

std::optional<std::chrono::system_clock::time_point> startFromName(const std::string& stem) {
	std::tm tm {};
	if (std::sscanf(stem.c_str(), "%4d-%2d-%2d_%2d-%2d-%2d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min,
	                &tm.tm_sec) != 6) {
		return std::nullopt;
	}
	tm.tm_year -= 1900;
	tm.tm_mon -= 1;
	tm.tm_isdst = -1;
	const std::time_t t = std::mktime(&tm);
	if (t == static_cast<std::time_t>(-1)) {
		return std::nullopt;
	}
	return std::chrono::system_clock::from_time_t(t);
}

}  // namespace

void SessionScanner::scan(Done done) {
	worker_.request_stop();
	worker_ = std::jthread([this, done = std::move(done)](std::stop_token stop) mutable {
		std::vector<SessionInfo> sessions;
		const auto dir = sessionLogDir();
		std::error_code ec;
		if (dir && std::filesystem::is_directory(*dir, ec)) {
			for (const auto& entry : std::filesystem::directory_iterator(*dir, ec)) {
				if (stop.stop_requested()) {
					return;
				}
				if (!entry.is_regular_file(ec) || entry.path().extension() != ".csv") {
					continue;
				}
				const auto size = static_cast<std::uint64_t>(entry.file_size(ec));
				const auto mtime = entry.last_write_time(ec);

				{
					std::lock_guard lock(mutex_);
					if (auto it = cache_.find(entry.path()); it != cache_.end() && it->second.size == size && it->second.mtime == mtime) {
						sessions.push_back(it->second.info);
						continue;
					}
				}

				SessionInfo info;
				info.path = entry.path();
				info.size = size;
				const auto ends = readCsvEnds(info.path);
				if (auto start = startFromName(info.path.stem().string())) {
					info.start = *start;
				} else if (ends.first) {
					info.start = std::chrono::system_clock::time_point(
					    std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds(ends.first->ts)));
				} else {
					info.start = std::chrono::clock_cast<std::chrono::system_clock>(mtime);
				}
				if (ends.first && ends.last && ends.last->ts >= ends.first->ts) {
					info.duration = std::chrono::seconds((ends.last->ts - ends.first->ts) / 1'000'000'000ull);
				}
				info.crashed = ends.last && icontains(ends.last->message, "connection lost");

				{
					std::lock_guard lock(mutex_);
					cache_[entry.path()] = {size, mtime, info};
				}
				sessions.push_back(std::move(info));
			}
		}
		std::ranges::sort(sessions, [](const SessionInfo& a, const SessionInfo& b) { return a.start > b.start; });
		post_([done = std::move(done), sessions = std::move(sessions)]() mutable { done(std::move(sessions)); });
	});
}

}  // namespace kenzo
