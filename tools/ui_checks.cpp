#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <print>
#include <rtxui/headless.hpp>
#include <set>
#include <stdexcept>
#include <thread>

#include "ui/app.hpp"
#include "core/format.hpp"
#include "core/wire.hpp"

int main() try {
	const auto path = std::filesystem::temp_directory_path() / ("kenzo-ui-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".csv");
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); }
	} cleanup {path};
	{
		std::ofstream csv(path);
		csv << "1792684753419544000,INFO,engine.cpp,10,Engine,Starting up,,\n"
		       "1792684753519544000,WARNING,assets.cpp,88,Assets,Missing texture,,\n"
		       "1792684753619544000,ERROR,render.cpp,120,Render,Shader failed,\"";
		for (int i = 0; i < 50; ++i) csv << "Detail line " << i << " with enough words to wrap across a narrow terminal pane.\n";
		csv << "\",\"";
		for (int i = 0; i < 50; ++i) csv << "Frame " << i << "\n";
		csv << "\"\n1792684753719544000,CRITICAL,window.cpp,33,Window,Window failed,,\n"
		       "1792684753819544000,TRACE,time.cpp,9,Time,Tick,,\n";
	}
	_putenv_s("RTXUI_HEADLESS", "120x30");
	_putenv_s("RTXUI_STRICT", "1");
	kenzo::ui::Options options;
	std::string copied;
	options.clipboard = [&](std::string_view text) { copied = text; };
	options.csv = path;
	options.reducedMotion = true;
	auto app = rtxui::Ref<kenzo::ui::App>::New(options);
	rtxui::TimelineScreen screen(app, 120, 30);
	const auto settle = [&] {
		for (int i = 0; i < 25; ++i) {
			screen.Advance(16);
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
	};
	const auto input = [&](std::string_view bytes) { screen.Input(bytes); settle(); };
	const auto check = [](bool condition, const char* message) {
		if (!condition) throw std::runtime_error(message);
	};
	std::vector<std::byte> wireBytes = {std::byte {0x0a}, std::byte {0}};
	for (unsigned sev : {0u, 1u, 2u, 3u, 4u, 9u}) {
		for (unsigned byte : {0x0au, 2u, 0x10u, sev}) wireBytes.push_back(static_cast<std::byte>(byte));
	}
	const auto decoded = kenzo::wire::decodeBatch(wireBytes);
	check(decoded && decoded->size() == 7, "protobuf severity fixture decodes");
	check((*decoded)[0].sev == kenzo::kTrace && (*decoded)[1].sev == kenzo::kTrace, "omitted and explicit zero severity are trace");
	for (std::size_t i = 2; i < 6; ++i) check((*decoded)[i].sev == i - 1, "explicit protobuf severity is preserved");
	check((*decoded)[6].sev == kenzo::kInfo, "unknown severity fallback");
	settle();
	check(app->rows.size() == 5, "fixture loaded");
	const auto initialAccent = app->grid_accent;
	check(app->rows[2].more == "…", "details ellipsis");
	input("s");
	input(std::string(1, '\0'));
	check(app->rows.size() == 1 && app->rows.front().sk0 == "Assets", "Ctrl-Space solos selected sink");
	input(std::string(1, '\0'));
	check(app->rows.size() == 5, "Ctrl-Space again restores sinks");
	input("2");
	const auto mixedSinks = app->sink_rows;
	check(mixedSinks[0].k1 == "i", "selected sink has Info disabled");
	input("I");
	check(app->sink_rows[0].k0 == "t" && app->sink_rows[0].k1 == "i on" &&
	      app->sink_rows[0].k2 == "w" && app->sink_rows[0].k3 == "e", "Shift-I inverts individual severity toggles");
	check(app->sink_rows[1].chk == "○", "Shift-I also inverts fully enabled sinks");
	input("I");
	for (std::size_t i = 0; i < mixedSinks.size(); ++i) {
		const auto& before = mixedSinks[i];
		const auto& after = app->sink_rows[i];
		check(before.k0 == after.k0 && before.k1 == after.k1 && before.k2 == after.k2 && before.k3 == after.k3,
		      "two Shift-I presses restore each sink's severity selections");
	}
	input("2");
	input("q");
	input(" ");
	check(app->show_ctx, "Space opens context menu");
	input("j");
	check(app->ctx_items[1].cls == "sel", "vim context highlight");
	std::string hoverBurst;
	for (int i = 0; i < 32; ++i) {
		hoverBurst += "\x1b[<35;" + std::to_string(app->ctx_x + 4 + i % 3) + ";" + std::to_string(app->ctx_y + 4) + "M";
	}
	input(hoverBurst);
	check(app->ctx_items[2].cls == "sel", "hover burst selects final context item");
	input("k");
	check(app->ctx_items[1].cls == "sel", "keyboard selection still works after hover burst");
	input("h");
	check(app->show_ctx_sub && app->ctx_sub_items[1].key == "hh", "hide submenu shortcuts");
	input("h");
	check(app->rows.size() == 4 && !app->show_ctx, "hh hides sink");
	input("u");
	check(app->rows.size() == 5, "undo hide");
	input("hf");
	check(app->rows.size() == 4, "table hf hides severity");
	input("u");
	input("ggjj");
	for (char key : {'y', 'm', 'b'}) {
		input(" ");
		check(app->ctx_items.size() == 3 && app->ctx_items[2].key == "h" && app->ctx_items[2].arrow == "▸", "Hide submenu arrow");
		input("y");
		check(app->show_ctx_sub && app->ctx_sub_items[0].key == "yy" && app->ctx_sub_items[1].key == "ym" && app->ctx_sub_items[2].key == "yb", "y opens copy submenu");
		check(app->ctx_sub_x == app->ctx_x + 27, "submenu shares the parent border");
		input(std::string(1, key));
		check(!app->show_ctx && !app->show_ctx_sub, "copy action closes context menu");
		if (key == 'y') check(copied.find("Shader failed") != std::string::npos && copied.find("Frame 0") != std::string::npos, "yy copies full entry");
		if (key == 'm') check(copied == "Shader failed", "ym copies message");
		if (key == 'b') check(copied.starts_with("Frame 0") && copied.find("Frame 1") != std::string::npos, "yb copies backtrace");
	}
	input(" ");
	screen.Click(app->ctx_x + 3, app->ctx_y + 2);
	settle();
	check(app->show_ctx_sub && app->ctx_sub_items[1].key == "ym", "mouse opens copy submenu");
	screen.Click(app->ctx_sub_x + 3, app->ctx_sub_y + 2);
	settle();
	check(!app->show_ctx && copied == "Shader failed", "mouse copies message and closes context menu");
	input("/sink:render");
	check(app->rows.size() == 1 && app->rows[0].sk1 == "Render", "sink search highlight");
	input("\x1b\x1b");
	input("\x1b\x1b");
	input("/file:render");
	check(app->rows.size() == 1 && app->rows[0].f1 == "render", "file search highlight");
	input("\x1b\x1b");
	input("\x1b\x1b");
	const auto time = kenzo::fmt::clockTime(1792684753419544000ull).substr(0, 5);
	input("/time:" + time);
	check(app->rows.size() == 5 && app->rows[0].t1 == time, "time search highlight");
	input("\x1b\x1b");
	input("\x1b\x1b");
	input("ggjj");
	input("o");
	check(app->show_detail && app->dv_has_bt, "open detail with backtrace");
	check(app->dv_msg_lines[1].cls == "sep" && app->dv_msg_lines[2].cls == "det", "details separator directly follows message without labels or padding");
	const auto first = app->dv_msg_lines.front().text;
	input("j");
	check(app->dv_msg_lines.front().text != first, "j scrolls detail");
	input("k");
	check(app->dv_msg_lines.front().text == first, "k scrolls detail back");
	const int left = (120 - app->dv_w) / 2 + 2;
	const int top = (30 - app->dv_h) / 2 + 4;
	input("\x1b[<65;" + std::to_string(left + 4) + ";" + std::to_string(top + 3) + "M");
	check(app->dv_msg_lines.front().text != first, "mouse wheel still scrolls detail");
	input("g");
	screen.Click(left + app->dv_lw - 3, top + app->dv_lh - 3);
	settle();
	check(app->dv_msg_lines.front().text != first, "mouse scrollbar jumps message pane");
	input("l");
	const auto bt = app->dv_bt_lines.front().text;
	input("j");
	check(app->dv_bt_lines.front().text != bt, "backtrace scrolls independently");
	_putenv_s("RTXUI_HEADLESS", "80x30");
	screen.Resize(80, 30);
	settle();
	check(app->dv_layout == "stack", "narrow detail stacks panes");
	for (const auto& line : app->dv_msg_lines) {
		if (line.cls == "det") check(line.text.size() <= static_cast<std::size_t>(app->dv_lw - 5), "details wrap to pane width");
	}
	input("q");
	input("q");
	check(app->show_menu && !app->grid_rows.empty(), "menu background renders");
	check(screen.Text().find("enter ip") != std::string::npos, "menu address label");
	check(app->grid_accent != initialAccent, "returning to menu picks a different accent");
	const auto background = app->grid_rows;
	settle();
	check(app->grid_rows == background, "reduced motion keeps background static");
	const std::array accents = {"red", "green", "blue", "magenta", "orange", "yellow", "cyan", "beige"};
	const std::array<std::array<int, 3>, 8> rgb = {{{255, 22, 89}, {0, 255, 64}, {41, 173, 255}, {173, 115, 255},
	                                            {255, 163, 0}, {255, 236, 39}, {115, 255, 234}, {255, 204, 170}}};
	for (std::size_t i = 0; i < accents.size(); ++i) {
		app->grid_accent = std::string("accent-") + accents[i];
		settle();
		bool checked = false;
		for (const auto& run : app->grid_rows) {
			if (run.y != 0 || run.cls == "g0") continue;
			const auto at = run.text.find_first_not_of(' ');
			if (at == std::string::npos) continue;
			const auto color = screen.ForegroundAt(run.x + static_cast<int>(at), run.y);
			const int level = run.cls.back() - '0';
			const double fraction = std::array {0.0, 0.12, 0.25, 0.42, 0.62, 0.84, 1.0}[static_cast<std::size_t>(level)];
			const auto expected = [&](int channel) {
				const double base = rgb[i][static_cast<std::size_t>(channel)];
				return static_cast<int>(std::lround(level == 6 ? base * 0.8 + 255 * 0.2 : (channel == 0 ? 18 : 17) * (1 - fraction) + base * fraction));
			};
			const auto ink = static_cast<std::uint8_t>(std::stoi(run.ink.substr(at * 2, 2), nullptr, 16));
			const auto shaded = rtxui::Blend(rtxui::Color::RGBA(static_cast<std::uint8_t>(expected(0)), static_cast<std::uint8_t>(expected(1)),
				static_cast<std::uint8_t>(expected(2)), ink), rtxui::Color::RGB(18, 17, 17));
			check(color == shaded, "accent palette and per-cell ink reach rendered background");
			checked = true;
			break;
		}
		check(checked, "colored background sample exists");
		std::set<unsigned> shades;
		for (const auto& run : app->grid_rows) {
			if (run.y > 2) break;
			int column = 0;
			for (std::size_t p = 0; p < run.text.size(); ++p) {
				if ((static_cast<unsigned char>(run.text[p]) & 0xc0) == 0x80) continue;
				if (run.text[p] != ' ') {
					const auto color = screen.ForegroundAt(run.x + column, run.y);
					shades.insert((static_cast<unsigned>(color.r) << 16) | (static_cast<unsigned>(color.g) << 8) | color.b);
				}
				++column;
			}
		}
		check(shades.size() > 8, "background retains fine color variation without tile-sized bands");
		const auto frame = screen.Text();
		const auto diamond = frame.find("◆");
		check(diamond != std::string::npos, "menu diamond exists");
		const auto lineStart = frame.rfind('\n', diamond);
		const auto start = lineStart == std::string::npos ? 0 : lineStart + 1;
		int x = 0, y = 0;
		for (std::size_t p = 0; p < diamond; ++p) {
			if (frame[p] == '\n') ++y;
			if (p >= start && (static_cast<unsigned char>(frame[p]) & 0xc0) != 0x80) ++x;
		}
		const auto diamondColor = screen.ForegroundAt(x, y);
		check(diamondColor.r == rgb[i][0] && diamondColor.g == rgb[i][1] && diamondColor.b == rgb[i][2], "diamond matches selected accent");
	}
	input("/");
	check(app->show_sessions, "slash opens more sessions");
	input("q");
	check(app->show_menu && !app->show_sessions, "closing sessions returns to menu");
	std::println("UI checks passed");
} catch (const std::exception& error) {
	std::println(stderr, "UI check failed: {}", error.what());
	return 1;
}
