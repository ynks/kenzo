#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace kenzo::fmt {

using Clock = std::chrono::system_clock;

std::string relativeTime(Clock::time_point then, Clock::time_point now = Clock::now());

std::string duration(std::chrono::seconds d);

std::string fileSize(std::uint64_t bytes);

std::string clockTime(std::uint64_t ns);

std::string offsetTime(std::uint64_t ns, std::uint64_t origin);

std::string fullTime(std::uint64_t ns);

std::string_view baseName(std::string_view path);

}  // namespace kenzo::fmt
