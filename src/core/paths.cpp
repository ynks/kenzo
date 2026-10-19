#include "core/paths.hpp"

#include <cstdlib>

namespace kenzo {
namespace {

std::optional<std::filesystem::path> envPath(const char* name) {
	const char* value = std::getenv(name);
	if (value == nullptr || *value == '\0') {
		return std::nullopt;
	}
	return std::filesystem::path(value);
}

}  // namespace

std::optional<std::filesystem::path> sessionLogDir() {
#ifdef _WIN32
	auto base = envPath("APPDATA");
#else
	auto base = envPath("XDG_CONFIG_HOME");
	if (!base) {
		if (auto home = envPath("HOME")) {
			base = *home / ".config";
		}
	}
#endif
	if (!base) {
		return std::nullopt;
	}
	return *base / "ToastEngine" / "logs";
}

}  // namespace kenzo
