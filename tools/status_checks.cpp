#include <chrono>
#include <filesystem>
#include <fstream>
#include <print>
#include <rtxui/headless.hpp>
#include <stdexcept>
#include <thread>

#include "ui/app.hpp"

int main() try {
	const auto path = std::filesystem::temp_directory_path() / ("kenzo-counts-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".csv");
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); }
	} cleanup {path};
	{
		std::ofstream csv(path);
		for (const auto* severity : {"TRACE", "INFO", "WARNING", "ERROR", "CRITICAL"}) {
			for (int i = 0; i < 1001; ++i) csv << "1792684753419544000," << severity << ",test.cpp,10,Engine,Log,,\n";
		}
	}
	_putenv_s("RTXUI_HEADLESS", "200x30");
	_putenv_s("RTXUI_STRICT", "1");
	kenzo::ui::Options options;
	options.csv = path;
	options.reducedMotion = true;
	auto app = rtxui::Ref<kenzo::ui::App>::New(options);
	rtxui::TimelineScreen screen(app, 200, 30);
	const auto settle = [&] {
		for (int i = 0; i < 25; ++i) {
			screen.Advance(16);
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
	};
	const auto check = [](bool condition, const char* message) { if (!condition) throw std::runtime_error(message); };
	for (int i = 0; i < 40 && app->cnt0 != "1001"; ++i) settle();
	check(app->cnt0 == "1001" && app->cnt1 == "1001" && app->cnt2 == "1001" && app->cnt3 == "2002", "full severity counts");
	check(app->st_show_hints, "wide terminal keeps centered shortcuts");
	for (int width : {120, 80, 60, 50, 40, 24, 16}) {
		_putenv_s("RTXUI_HEADLESS", (std::to_string(width) + "x30").c_str());
		screen.Resize(width, 30);
		settle();
		const auto frame = screen.Text();
		const auto end = frame.find_last_not_of('\n');
		const auto start = frame.rfind('\n', end);
		const auto status = frame.substr(start == std::string::npos ? 0 : start + 1, end - start);
		const auto totalAt = frame.find("5005 logs");
		check(totalAt != std::string::npos && frame.find("5005 logs", totalAt + 1) == std::string::npos, "total appears once in search bar");
		check(status.find("5005 logs") == std::string::npos, "status bar does not duplicate the total");
		if (app->st_show_levels) {
			check(status.find(app->cnt_label3 + " 2002") != std::string::npos, "error count is not clipped");
			check(status.find(app->cnt_label0 + " 1001") != std::string::npos, "trace count is not clipped");
		}
		if (width <= 80) check(!app->st_show_hints, "shortcut hints yield space to counts");
	}
	screen.Input("f");
	settle();
	check(app->sev_rows[0].count == "1001" && app->sev_rows[3].count == "2002", "popup counts are not capped");
	screen.Input("q");
	screen.Input("s");
	settle();
	check(app->sink_rows[0].count == "5005", "sink counts are not capped");
	std::println("Status checks passed");
} catch (const std::exception& error) {
	std::println(stderr, "Status check failed: {}", error.what());
	return 1;
}
