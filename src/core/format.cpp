#include "core/format.hpp"

#include <ctime>
#include <format>

namespace kenzo::fmt {
namespace {

std::tm localTime(std::time_t t) {
	std::tm tm {};
#ifdef _WIN32
	localtime_s(&tm, &t);
#else
	localtime_r(&t, &tm);
#endif
	return tm;
}

constexpr const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

std::string plural(long n, const char* unit) {
	return std::format("{} {}{} ago", n, unit, n == 1 ? "" : "s");
}

}  // namespace

std::string relativeTime(Clock::time_point then, Clock::time_point now) {
	using namespace std::chrono;
	const auto elapsed = duration_cast<seconds>(now - then);
	if (elapsed < seconds(0) || elapsed < minutes(1)) {
		return "a moment ago";
	}
	if (elapsed < hours(1)) {
		return plural(static_cast<long>(duration_cast<minutes>(elapsed).count()), "minute");
	}
	if (elapsed < hours(24)) {
		return plural(static_cast<long>(duration_cast<hours>(elapsed).count()), "hour");
	}

	const auto t = localTime(Clock::to_time_t(then));
	const auto n = localTime(Clock::to_time_t(now));
	const auto dayNumber = [](const std::tm& tm) {
		std::tm copy = tm;
		copy.tm_hour = 12;
		copy.tm_min = copy.tm_sec = 0;
		return static_cast<long>(std::mktime(&copy) / 86400);
	};
	const long days = dayNumber(n) - dayNumber(t);
	if (days <= 1) {
		return "yesterday";
	}
	if (days < 7) {
		return "this week";
	}
	return std::format("{:02} {} {}", t.tm_mday, kMonths[t.tm_mon], t.tm_year + 1900);
}

std::string duration(std::chrono::seconds d) {
	const long total = static_cast<long>(d.count() < 0 ? 0 : d.count());
	if (total < 60) {
		return std::format("{} s", total);
	}
	const long minutes = total / 60;
	if (minutes < 60) {
		return std::format("{} min", minutes);
	}
	const long hours = minutes / 60;
	const long rest = minutes % 60;
	return rest == 0 ? std::format("{} h", hours) : std::format("{} h {} min", hours, rest);
}

std::string fileSize(std::uint64_t bytes) {
	constexpr double kK = 1000.0;
	if (bytes < 1000) {
		return std::format("{} B", bytes);
	}
	double v = static_cast<double>(bytes) / kK;
	const char* unit = "kB";
	if (v >= kK) {
		v /= kK;
		unit = "MB";
	}
	if (v >= kK) {
		v /= kK;
		unit = "GB";
	}
	return v < 10 ? std::format("{:.1f} {}", v, unit) : std::format("{:.0f} {}", v, unit);
}

std::string clockTime(std::uint64_t ns) {
	const auto secs = static_cast<std::time_t>(ns / 1'000'000'000ull);
	const auto ms = static_cast<unsigned>((ns / 1'000'000ull) % 1000);
	const auto tm = localTime(secs);
	return std::format("{:02}:{:02}:{:02}.{:03}", tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
}

std::string offsetTime(std::uint64_t ns, std::uint64_t origin) {
	const std::uint64_t delta = ns >= origin ? ns - origin : 0;
	const auto ms = delta / 1'000'000ull;
	if (ms >= 3'600'000) {
		return std::format("+{}h{:02}m{:02}s", ms / 3'600'000, (ms / 60'000) % 60, (ms / 1000) % 60);
	}
	if (ms >= 60'000) {
		return std::format("+{}m{:02}.{:03}s", ms / 60'000, (ms / 1000) % 60, ms % 1000);
	}
	return std::format("+{}.{:03}s", ms / 1000, ms % 1000);
}

std::string fullTime(std::uint64_t ns) {
	const auto secs = static_cast<std::time_t>(ns / 1'000'000'000ull);
	const auto ms = static_cast<unsigned>((ns / 1'000'000ull) % 1000);
	const auto tm = localTime(secs);
	return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
	                   tm.tm_min, tm.tm_sec, ms);
}

std::string_view baseName(std::string_view path) {
	const auto pos = path.find_last_of("/\\");
	return pos == std::string_view::npos ? path : path.substr(pos + 1);
}

}  // namespace kenzo::fmt
