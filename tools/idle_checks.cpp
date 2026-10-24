#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <print>
#include <rtxui/headless.hpp>
#include <stdexcept>
#include <thread>

#include "ui/app.hpp"

int main() try {
	using Clock = std::chrono::steady_clock;
	const auto path = std::filesystem::temp_directory_path() / ("kenzo-idle-" + std::to_string(Clock::now().time_since_epoch().count()) + ".csv");
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); }
	} cleanup {path};
	std::ofstream(path) << "1792684753419544000,TRACE,test.cpp,1,Test,Test,,\n";
	_putenv_s("RTXUI_HEADLESS", "100x30");
	_putenv_s("RTXUI_STRICT", "1");
	{
		auto startup = rtxui::Ref<kenzo::ui::App>::New(kenzo::ui::Options {});
		if (startup->grid_opacity != "0.5" || !std::ranges::any_of(startup->grid_rows, [](const auto& run) { return run.text.find_first_not_of(' ') != std::string::npos; })) {
			throw std::runtime_error("first launch must have a visible background immediately");
		}
	}
	kenzo::ui::Options options;
	options.csv = path;
	auto now = Clock::now();
	options.menuClock = [&] { return now; };
	auto app = rtxui::Ref<kenzo::ui::App>::New(options);
	rtxui::TimelineScreen screen(app, 100, 30);
	screen.Input("q");
	const auto initial = app->grid_accent;
	const auto advance = [&](int milliseconds) {
		now += std::chrono::milliseconds(milliseconds);
		screen.Advance(milliseconds);
	};
	const auto check = [](bool condition, const char* message) { if (!condition) throw std::runtime_error(message); };
	check(app->grid_opacity == "0", "menu opens with an entrance");
	advance(1500);
	check(std::stod(app->grid_opacity) > 0 && std::stod(app->grid_opacity) < 1, "entrance is still revealing at 1.5 seconds");
	advance(1500);
	check(app->grid_opacity == "1", "entrance completes after three seconds");
	screen.Input("j");
	advance(59000);
	check(app->grid_accent == initial && app->grid_opacity == "1", "no automatic change before one minute");
	screen.Input("\x1b[<35;2;2M");
	advance(2000);
	check(app->grid_accent == initial && app->grid_opacity == "1", "mouse movement resets idle timeout");
	advance(58000);
	advance(2999);
	check(app->grid_accent == initial, "dissolve lasts the full three seconds");
	advance(66);
	check(app->show_menu && app->grid_accent != initial && app->grid_opacity == "0", "idle recolors only after the dissolve");
	advance(1500);
	check(std::stod(app->grid_opacity) > 0 && std::stod(app->grid_opacity) < 1, "new background has the same long entrance");
	advance(1500);
	check(app->grid_opacity == "1", "new background finishes entrance after three seconds");
	const auto automaticAccent = app->grid_accent;
	screen.Input("\t");
	advance(1500);
	check(app->grid_accent == automaticAccent, "Tab starts a dissolve rather than instantly swapping colors");
	screen.Input("\t");
	advance(1500);
	check(app->grid_accent != automaticAccent && app->grid_opacity == "0", "repeated Tab does not restart an active dissolve");
	advance(3000);
	check(app->grid_opacity == "1", "Tab-triggered entrance completes");
	std::println("Idle checks passed");
} catch (const std::exception& error) {
	std::println(stderr, "Idle check failed: {}", error.what());
	return 1;
}
