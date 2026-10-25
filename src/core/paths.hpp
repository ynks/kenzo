#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace kenzo {

std::optional<std::filesystem::path> sessionLogDir();

std::optional<std::filesystem::path> remoteLogDir();

// string() breaks on non ascii paths on windows
std::string pathUtf8(const std::filesystem::path& path);

}  // namespace kenzo
