#pragma once

#include <string>
#include <string_view>

namespace kenzo {

[[nodiscard]] bool isValidUtf8(std::string_view text);

void repairUtf8(std::string& text);

}  // namespace kenzo
