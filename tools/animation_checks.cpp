#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <print>
#include <rtxui/headless.hpp>

#include "ui/app.hpp"

int main(int argc, char** argv) {
	const int frames = argc > 1 ? std::max(60, std::atoi(argv[1])) : 240;
	const int width = argc > 2 ? std::atoi(argv[2]) : 120;
	const int height = argc > 3 ? std::atoi(argv[3]) : 40;
	const int moves = argc > 4 ? std::max(0, std::atoi(argv[4])) : 0;
	_putenv_s("RTXUI_HEADLESS", (std::to_string(width) + "x" + std::to_string(height)).c_str());
	_putenv_s("RTXUI_STRICT", "1");
	auto app = rtxui::Ref<kenzo::ui::App>::New(kenzo::ui::Options {});
	rtxui::TimelineScreen screen(app, width, height);
	using Clock = std::chrono::steady_clock;
	double first = 0, last = 0, worst = 0;
	const auto initial = app->grid_rows;
	const auto geometry = app->grid_rows;
	for (int i = 0; i < frames; ++i) {
		const auto start = Clock::now();
		if (moves > 0) {
			std::string burst;
			for (int j = 0; j < moves; ++j) {
				const int x = (i * moves + j) % width + 1;
				const int y = ((i * moves + j) / width * 7) % height + 1;
				burst += "\x1b[<35;" + std::to_string(x) + ";" + std::to_string(y) + "M";
			}
			screen.Input(burst);
		}
		screen.Advance(66);
		const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
		if (i < 30) first += ms / 30;
		if (i >= frames - 30) last += ms / 30;
		worst = std::max(worst, ms);
		if (app->grid_rows.size() > 480 || app->grid_rows.size() != geometry.size()) return 1;
		for (std::size_t j = 0; j < geometry.size(); ++j) {
			const auto& before = geometry[j];
			const auto& after = app->grid_rows[j];
			if (before.x != after.x || before.y != after.y || before.width != after.width) return 1;
		}
		if ((i + 1) % std::max(60, frames / 4) == 0) std::println("frame {}: {:.1f} ms, {} tiles, {} grid elements", i + 1, ms, app->grid_rows.size(), app->QueryElement(".grid").ChildCount());
	}
	std::println("{}x{}: first {:.1f} ms, last {:.1f} ms, worst {:.1f} ms", width, height, first, last, worst);
	if (initial == app->grid_rows || last > 66.0 || last > first * 3.0) return 1;
}
