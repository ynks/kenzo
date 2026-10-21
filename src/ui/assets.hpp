#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace kenzo::ui {

namespace embedded {
extern const std::string_view markupChunks[];
extern const std::size_t markupCount;
extern const std::string_view styleChunks[];
extern const std::size_t styleCount;
}  // namespace embedded

std::string loadView();

}  // namespace kenzo::ui
