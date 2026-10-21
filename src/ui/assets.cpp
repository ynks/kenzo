#include "ui/assets.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace kenzo::ui {
namespace {

constexpr std::array markupFiles = {"menu.html", "main.html", "overlays.html", "detail.html"};
constexpr std::array styleFiles = {"theme.css", "base.css", "main.css", "overlays.css"};

std::string readFile(const std::filesystem::path& path) {
	std::ifstream in(path, std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

}  // namespace

std::string loadView() {
	std::string markup, style;
	const char* dir = std::getenv("KENZO_ASSETS");
	if (dir != nullptr && *dir != '\0') {
		for (const char* name : markupFiles) markup += readFile(std::filesystem::path(dir) / name);
		for (const char* name : styleFiles) style += readFile(std::filesystem::path(dir) / name);
	} else {
		for (std::size_t i = 0; i < embedded::markupCount; ++i) markup += embedded::markupChunks[i];
		for (std::size_t i = 0; i < embedded::styleCount; ++i) style += embedded::styleChunks[i];
	}
	return "<div class=\"app {app_cls}\">" + markup + "</div><style>" + style + "</style>";
}

}  // namespace kenzo::ui
