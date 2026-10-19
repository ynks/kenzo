#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "core/record.hpp"

namespace kenzo::wire {

inline constexpr std::size_t kMaxFrame = 64u * 1024 * 1024;

std::expected<std::vector<RawRecord>, std::string> decodeBatch(std::span<const std::byte> bytes);

}  // namespace kenzo::wire
