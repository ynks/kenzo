#pragma once

#include <filesystem>
#include <optional>

namespace kenzo {

std::optional<std::filesystem::path> sessionLogDir();

}  // namespace kenzo
