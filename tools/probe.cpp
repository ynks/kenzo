#include <chrono>
#include <cstdlib>
#include <fstream>
#include <print>
#include <rtxui/headless.hpp>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "ui/app.hpp"

namespace {

std::vector<std::string> splitLines(const std::string& text) {
	std::vector<std::string> out;
	std::stringstream ss(text);
	for (std::string line; std::getline(ss, line);) out.push_back(line);
	return out;
}

std::vector<std::string> codepoints(const std::string& line) {
	std::vector<std::string> out;
	for (std::size_t i = 0; i < line.size();) {
		const auto c = static_cast<unsigned char>(line[i]);
		const std::size_t n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
		out.push_back(line.substr(i, n));
		i += n;
	}
	return out;
}

std::string hex(rtxui::Color c, rtxui::Color fallback) {
	if (c.a == 0) c = fallback;
	char buf[16];
	std::snprintf(buf, sizeof buf, "#%02x%02x%02x", c.r, c.g, c.b);
	return buf;
}

void writeHtml(const rtxui::TimelineScreen& screen, int width, int height, const std::string& path) {
	const auto lines = splitLines(screen.Text());
	const rtxui::Color defaultBg = rtxui::Color::RGB(0x12, 0x11, 0x11), defaultFg = rtxui::Color::RGB(0xfa, 0xfa, 0xfa);
	std::ofstream out(path);
	out << "<html><body style='margin:0;background:#121111'><pre style='margin:0;padding:8px;font:15px/1.15 Consolas,\"Cascadia Mono\",monospace;"
	       "color:#fafafa;background:#121111'>";
	for (int y = 0; y < height; ++y) {
		const auto cells = y < static_cast<int>(lines.size()) ? codepoints(lines[static_cast<std::size_t>(y)]) : std::vector<std::string> {};
		std::string runFg, runBg, run;
		const auto flush = [&] {
			if (run.empty()) return;
			out << "<span style='color:" << runFg << ";background:" << runBg << "'>" << run << "</span>";
			run.clear();
		};
		for (int x = 0; x < width; ++x) {
			const std::string fg = hex(screen.ForegroundAt(x, y), defaultFg), bg = hex(screen.BackgroundAt(x, y), defaultBg);
			if (fg != runFg || bg != runBg) {
				flush();
				runFg = fg;
				runBg = bg;
			}
			std::string ch = x < static_cast<int>(cells.size()) ? cells[static_cast<std::size_t>(x)] : " ";
			if (ch == "<") ch = "&lt;";
			if (ch == "&") ch = "&amp;";
			run += ch;
		}
		flush();
		out << "\n";
	}
	out << "</pre></body></html>";
}

}  // namespace

int main(int argc, char** argv) {
	int width = 120, height = 36, waitMs = 600;
	kenzo::ui::Options options;
	std::vector<std::string> script;
	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		if (i == 1 && arg.find('x') != std::string::npos) {
			std::sscanf(arg.c_str(), "%dx%d", &width, &height);
		} else if (arg == "--csv" && i + 1 < argc) {
			options.csv = argv[++i];
		} else if (arg == "--wait" && i + 1 < argc) {
			waitMs = std::atoi(argv[++i]);
		} else if (arg == "--reduced-motion") {
			options.reducedMotion = true;
		} else {
			script.push_back(arg);
		}
	}
	_putenv_s("RTXUI_HEADLESS", (std::to_string(width) + "x" + std::to_string(height)).c_str());

	auto app = rtxui::Ref<kenzo::ui::App>::New(std::move(options));
	rtxui::TimelineScreen screen(app, width, height);

	const auto settle = [&](int ms) {
		const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
		while (std::chrono::steady_clock::now() < end) {
			screen.Advance(16);
			std::this_thread::sleep_for(std::chrono::milliseconds(8));
		}
	};
	settle(waitMs);

	for (const std::string& item : script) {
		if (item.size() > 2 && item.front() == '<') {
			const std::string body = item.substr(1, item.size() - 2);
			int x = 0, y = 0, ms = 0;
			if (body == "Dump") {
				std::println("{}\n----", screen.Text());
			} else if (body.starts_with("Html:")) {
				writeHtml(screen, width, height, body.substr(5));
			} else if (body == "Enter") screen.Input("\r");
			else if (body == "Esc") screen.Input("\x1b");
			else if (body == "Tab") screen.Input("\t");
			else if (body == "Up") screen.Input("\x1b[A");
			else if (body == "Down") screen.Input("\x1b[B");
			else if (body == "BS") screen.Input("\x7f");
			else if (body == "C-Space") screen.Input(std::string(1, '\0'));
			else if (body.starts_with("C-") && body.size() == 3) screen.Input(std::string(1, static_cast<char>(body[2] & 0x1f)));
			else if (std::sscanf(body.c_str(), "Click:%d,%d", &x, &y) == 2) screen.Click(x, y);
			else if (std::sscanf(body.c_str(), "Right:%d,%d", &x, &y) == 2)
				screen.Input("\x1b[<2;" + std::to_string(x + 1) + ";" + std::to_string(y + 1) + "M\x1b[<2;" + std::to_string(x + 1) + ";" +
				             std::to_string(y + 1) + "m");
			else if (std::sscanf(body.c_str(), "Wheel:%d", &y) == 1)
				screen.Input(std::string("\x1b[<") + (y < 0 ? "64" : "65") + ";10;10M");
			else if (std::sscanf(body.c_str(), "Wait:%d", &ms) == 1) settle(ms);
			else std::println(stderr, "probe: unknown item {}", item);
		} else {
			screen.Input(item);
		}
		settle(40);
	}
	std::println("{}", screen.Text());
}
