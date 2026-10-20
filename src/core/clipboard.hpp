#pragma once

#include <string_view>

namespace kenzo {

bool copyToSystemClipboard(std::string_view text);

}  // namespace kenzo
