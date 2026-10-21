#include "ui/terminal.hpp"

#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace kenzo::ui {

std::pair<int, int> terminalSize() {
	if (const char* headless = std::getenv("RTXUI_HEADLESS")) {
		int w = 0, h = 0;
		if (std::sscanf(headless, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
			return {w, h};
		}
	}
#ifdef _WIN32
	CONSOLE_SCREEN_BUFFER_INFO info {};
	for (const DWORD which : {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE}) {
		if (GetConsoleScreenBufferInfo(GetStdHandle(which), &info)) {
			return {info.srWindow.Right - info.srWindow.Left + 1, info.srWindow.Bottom - info.srWindow.Top + 1};
		}
	}
#else
	winsize ws {};
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
		return {ws.ws_col, ws.ws_row};
	}
#endif
	return {120, 40};
}

}  // namespace kenzo::ui
