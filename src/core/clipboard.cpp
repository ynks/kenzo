#include "core/clipboard.hpp"

#include <cstdio>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace kenzo {

#ifdef _WIN32

bool copyToSystemClipboard(std::string_view text) {
	const int wide = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
	if (wide < 0) {
		return false;
	}
	HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (static_cast<SIZE_T>(wide) + 1) * sizeof(wchar_t));
	if (mem == nullptr) {
		return false;
	}
	auto* dst = static_cast<wchar_t*>(GlobalLock(mem));
	if (dst == nullptr) {
		GlobalFree(mem);  // only freed on failure
		return false;
	}
	MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), dst, wide);
	dst[wide] = L'\0';
	GlobalUnlock(mem);

	bool ok = false;
	for (int attempt = 0; attempt < 5 && !ok; ++attempt) {
		if (OpenClipboard(nullptr)) {
			EmptyClipboard();
			ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
			CloseClipboard();
		} else {
			Sleep(10);
		}
	}
	if (!ok) {
		GlobalFree(mem);  // only freed on failure
	}
	return ok;
}

#else

namespace {

bool pipeTo(const char* command, std::string_view text) {
	FILE* pipe = popen(command, "w");
	if (pipe == nullptr) {
		return false;
	}
	const bool wrote = std::fwrite(text.data(), 1, text.size(), pipe) == text.size();
	return pclose(pipe) == 0 && wrote;
}

}  // namespace

bool copyToSystemClipboard(std::string_view text) {
#ifdef __APPLE__
	return pipeTo("pbcopy 2>/dev/null", text);
#else
	return pipeTo("wl-copy 2>/dev/null", text) || pipeTo("xclip -selection clipboard 2>/dev/null", text) ||
	       pipeTo("xsel --clipboard --input 2>/dev/null", text);
#endif
}

#endif

}  // namespace kenzo
