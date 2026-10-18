#include "core/utf8.hpp"

#include <cstdint>

namespace kenzo {
namespace {

std::size_t sequenceLength(std::string_view s) {
	const auto b = [&](std::size_t i) { return static_cast<std::uint8_t>(s[i]); };
	const std::uint8_t c = b(0);
	if (c < 0x80) {
		return 1;
	}
	const auto cont = [&](std::size_t i) { return i < s.size() && (b(i) & 0xC0) == 0x80; };
	if (c >= 0xC2 && c <= 0xDF) {
		return cont(1) ? 2 : 0;
	}
	if (c >= 0xE0 && c <= 0xEF) {
		if (!cont(1) || !cont(2)) {
			return 0;  // overlong
		}
		if (c == 0xE0 && b(1) < 0xA0) {
			return 0;  // surrogate
		}
		if (c == 0xED && b(1) >= 0xA0) {
			return 0;
		}
		return 3;
	}
	if (c >= 0xF0 && c <= 0xF4) {
		if (!cont(1) || !cont(2) || !cont(3)) {
			return 0;
		}
		if (c == 0xF0 && b(1) < 0x90) {
			return 0;
		}
		if (c == 0xF4 && b(1) >= 0x90) {
			return 0;
		}
		return 4;
	}
	return 0;
}

}  // namespace

bool isValidUtf8(std::string_view text) {
	std::size_t i = 0;
	while (i < text.size()) {
		if (static_cast<std::uint8_t>(text[i]) < 0x80) {
			++i;
			continue;
		}
		const std::size_t n = sequenceLength(text.substr(i));
		if (n == 0) {
			return false;
		}
		i += n;
	}
	return true;
}

void repairUtf8(std::string& text) {
	if (isValidUtf8(text)) {
		return;
	}
	std::string out;
	out.reserve(text.size() + 8);
	std::size_t i = 0;
	while (i < text.size()) {
		const std::size_t n = static_cast<std::uint8_t>(text[i]) < 0x80 ? 1 : sequenceLength(std::string_view(text).substr(i));
		if (n == 0) {
			out += "\xEF\xBF\xBD";
			++i;
		} else {
			out.append(text, i, n);
			i += n;
		}
	}
	text = std::move(out);
}

}  // namespace kenzo
