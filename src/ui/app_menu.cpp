#include <algorithm>
#include <cmath>
#include <format>
#include <random>

#include "core/format.hpp"
#include "ui/app.hpp"

namespace kenzo::ui {
namespace {

constexpr std::array<const char*, 10> kSpinner = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
constexpr auto kBackgroundIdle = std::chrono::minutes(1);
constexpr double kBackgroundTransitionSeconds = 3.0;

std::string repeat(std::string_view unit, int count) {
	std::string out;
	for (int i = 0; i < count; ++i) {
		out += unit;
	}
	return out;
}

std::string sessionInfo(const SessionInfo& s) {
	return std::format("{} ({})", fmt::duration(s.duration), fmt::fileSize(s.size));
}

}  // namespace

Clock::time_point App::menuNow() const {
	return options_.menuClock ? options_.menuClock() : Clock::now();
}

void App::resetMenuIdle() {
	menuActivityAt_ = menuNow();
	idleFade_ = IdleFade::None;
	backgroundCoverage_ = 1.0;
	backgroundProgress_ = 1.0;
	grid_opacity = "1";
}

void App::beginBackgroundEntrance(bool startup) {
	if (options_.reducedMotion) return;
	idleFade_ = startup ? IdleFade::Startup : IdleFade::FadeIn;
	idleFadeAt_ = menuNow();
	backgroundProgress_ = 0.0;
	backgroundCoverage_ = startup ? 1.0 : 0.0;
	grid_opacity = startup ? "0.5" : "0";
}

void App::beginBackgroundChange() {
	if (idleFade_ != IdleFade::None) return;
	menuActivityAt_ = menuNow();
	if (options_.reducedMotion) {
		randomizeBackground();
		return;
	}
	idleFade_ = IdleFade::Dissolve;
	idleFadeAt_ = menuActivityAt_;
	backgroundProgress_ = 0.0;
}

void App::updateMenuIdle() {
	if (overlay_ != Overlay::None || lanFocus_) {
		resetMenuIdle();
		return;
	}
	const auto now = menuNow();
	if (idleFade_ == IdleFade::None) {
		if (now - menuActivityAt_ < kBackgroundIdle) return;
		beginBackgroundChange();
	}
	const double progress = std::clamp(std::chrono::duration<double>(now - idleFadeAt_).count() / kBackgroundTransitionSeconds, 0.0, 1.0);
	backgroundProgress_ = progress;
	const double eased = progress * progress * (3.0 - 2.0 * progress);
	if (idleFade_ == IdleFade::Dissolve) {
		backgroundCoverage_ = 1.0 - eased;
		if (progress >= 1.0) {
			randomizeBackground();
			beginBackgroundEntrance();
		}
	} else {
		const bool startup = idleFade_ == IdleFade::Startup;
		backgroundCoverage_ = startup ? 1.0 : eased;
		grid_opacity = std::format("{:.3f}", startup ? 0.5 + eased * 0.5 : eased);
		if (progress >= 1.0) {
			idleFade_ = IdleFade::None;
			grid_opacity = "1";
		}
	}
}

void App::randomizeBackground() {
	static std::mt19937 random(std::random_device {}());
	static constexpr std::array names = {"red", "green", "blue", "magenta", "orange", "yellow", "cyan", "beige"};
	int next = std::uniform_int_distribution<int>(0, backgroundAccent_ < 0 ? 7 : 6)(random);
	if (backgroundAccent_ >= 0 && next >= backgroundAccent_) ++next;
	backgroundAccent_ = next;
	grid_accent = std::format("accent-{}", names[static_cast<std::size_t>(next)]);
}

void App::buildGrid() {
	const int width = std::min(cols_, 320);
	const int height = std::min(rowsTotal_, 120);
	const double t = options_.reducedMotion ? 0.0 : static_cast<double>(frame_) * 0.066;
	const double swirl = idleFade_ == IdleFade::Dissolve ? backgroundProgress_
	                   : (idleFade_ == IdleFade::FadeIn || idleFade_ == IdleFade::Startup) ? 1.0 - backgroundProgress_ : 0.0;
	const double angle = swirl * swirl * 2.4;
	const double turnCos = std::cos(angle), turnSin = std::sin(angle);
	const double zoom = 1.0 + swirl * 1.7;
	const auto field = [&](double x, double y) {
		const double px = (x - width * 0.5) * 0.055;
		const double py = (y - height * 0.5) * 0.13;
		const double u = (px * turnCos - py * turnSin) / zoom;
		const double v = (px * turnSin + py * turnCos) / zoom;
		const double warp = u + 0.65 * std::sin(v * 0.9 + t * 0.27);
		const double flow = std::sin(warp * 1.35 + std::sin(v + t * 0.34) * 1.6 - t * 0.48)
		                  + 0.45 * std::sin(v * 1.7 - u * 0.65 + t * 0.31);
		const double crest = std::max(0.0, 1.0 - std::abs(flow) * 0.9);
		const double crest2 = crest * crest;
		const double ribbon = crest2 * crest2;
		const double radius = std::hypot(u + 1.4 * std::sin(t * 0.13), v + 0.8 * std::cos(t * 0.17));
		const double ring1 = std::max(0.0, std::cos(radius * 3.2 - t * 1.4));
		const double ring2 = ring1 * ring1;
		const double ring4 = ring2 * ring2;
		const double ring8 = ring4 * ring4;
		const double ring = ring8 * ring8;
		return std::min(1.0, ribbon * 0.85 + ring * 0.55);
	};
	constexpr int dots[4][2] = {{0, 3}, {1, 4}, {2, 5}, {6, 7}};
	constexpr unsigned thresholds[8][8] = {
		{0, 48, 12, 60, 3, 51, 15, 63},
		{32, 16, 44, 28, 35, 19, 47, 31},
		{8, 56, 4, 52, 11, 59, 7, 55},
		{40, 24, 36, 20, 43, 27, 39, 23},
		{2, 50, 14, 62, 1, 49, 13, 61},
		{34, 18, 46, 30, 33, 17, 45, 29},
		{10, 58, 6, 54, 9, 57, 5, 53},
		{42, 26, 38, 22, 41, 25, 37, 21},
	};

	const int tilesPerRow = std::max(1, 480 / std::max(1, height));
	const int tileWidth = std::max(8, (width + tilesPerRow - 1) / tilesPerRow);
	const int tileCount = (width + tileWidth - 1) / tileWidth;
	grid_rows.resize(static_cast<std::size_t>(tileCount * height));
	for (int y = 0; y < height; ++y) {
		for (int tile = 0; tile < tileCount; ++tile) {
			auto& run = grid_rows[static_cast<std::size_t>(y * tileCount + tile)];
			run.x = tile * tileWidth;
			run.y = y;
			run.width = std::min(tileWidth, width - run.x);
			run.text.clear();
			run.ink.clear();
			run.cls = "g6";
			for (int x = run.x; x < run.x + run.width; ++x) {
				unsigned mask = 0;
				double glow = 0.0;
				for (int dy = 0; dy < 4; ++dy) {
					for (int dx = 0; dx < 2; ++dx) {
						const double sampleX = x + (dx + 0.5) * 0.5;
						const double sampleY = y + (dy + 0.5) * 0.25;
						const double energy = field(sampleX, sampleY);
						glow += energy / 8.0;
						bool visible = true;
						if (backgroundCoverage_ < 1.0) {
							const double radius = std::min(1.0, std::hypot(
							    (sampleX - width * 0.5) / (width * 0.5), (sampleY - height * 0.5) / (height * 0.5)) / std::sqrt(2.0));
							std::uint32_t noise = static_cast<std::uint32_t>(x * 2 + dx) * 0x9e3779b9u
							                    ^ static_cast<std::uint32_t>(y * 4 + dy) * 0x85ebca6bu;
							noise ^= noise >> 16;
							noise *= 0x7feb352du;
							noise ^= noise >> 15;
							const double grain = static_cast<double>(noise & 0xffffu) / 65536.0;
							const double threshold = idleFade_ == IdleFade::FadeIn ? radius * 0.78 + grain * 0.22
							                       : (1.0 - radius) * 0.35 + grain * 0.65;
							visible = threshold < backgroundCoverage_;
						}
						const double threshold = (thresholds[(y * 4 + dy) % 8][(x * 2 + dx) % 8] + 0.5) / 64.0;
						if (visible && energy > threshold) mask |= 1u << dots[dy][dx];
					}
				}
				constexpr char hex[] = "0123456789abcdef";
				const auto ink = static_cast<unsigned>(std::clamp(std::lround((0.05 + 0.95 * glow) * 255), 0l, 255l));
				run.ink += hex[ink >> 4];
				run.ink += hex[ink & 15];
				auto& text = run.text;
				if (mask == 0) {
					text += ' ';
				} else {
					const unsigned cp = 0x2800 + mask;
					text += static_cast<char>(0xE0 | (cp >> 12));
					text += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
					text += static_cast<char>(0x80 | (cp & 0x3F));
				}
			}
		}
	}
}

void App::refreshSessions() {
	lastSessionScan_ = Clock::now();
	scanner_->scan([this](std::vector<SessionInfo> found) { sessions_ = std::move(found); });
}

bool App::lanChanged() {
	menu_error.clear();
	return true;
}

void App::buildMenu() {
	const std::string spin = kSpinner[static_cast<std::size_t>(spinner_) % kSpinner.size()];

	const bool local = source_ == Source::Localhost;
	menu_cls0 = menuSel_ == 0 ? "sel" : "";
	if (local && link_ == Link::Connected) {
		menu_icon0 = "●";
		menu_note0 = "connected";
		menu_note_cls0 = "ok";
	} else if (local) {
		menu_icon0 = spin;
		menu_note0 = "";
		menu_note_cls0 = "";
	} else {
		menu_icon0 = "○";
		menu_note0 = "";
		menu_note_cls0 = "";
	}

	const bool lan = source_ == Source::Lan;
	menu_cls1 = menuSel_ == 1 ? "sel" : "";
	lan_editing = lanFocus_ || !lan_.empty();
	lan_idle = !lan_editing;
	if (lanFocus_) {
		lan_before = lan_.before();
		lan_under = lan_.under();
		lan_after = lan_.after();
	} else {
		lan_before = lan_.text();
		lan_under.clear();
		lan_after.clear();
	}
	if (lan && link_ == Link::Connected) {
		menu_icon1 = "●";
		menu_note1 = "connected";
		menu_note_cls1 = "ok";
	} else if (lan) {
		menu_icon1 = spin;
		menu_note1 = "";
		menu_note_cls1 = "";
	} else {
		menu_icon1 = lanFocus_ ? "›" : "○";
		menu_note1 = "";
		menu_note_cls1 = "";
	}

	menu_rule = repeat("─", 48);

	menu_sessions.clear();
	const std::size_t shown = std::min<std::size_t>(3, sessions_.size());
	for (std::size_t i = 0; i < shown; ++i) {
		const SessionInfo& s = sessions_[i];
		MenuSessionRow row;
		row.cls = menuSel_ == static_cast<int>(2 + i) ? "sel" : "";
		row.icon = s.crashed ? "!" : "◷";
		row.when = fmt::relativeTime(s.start);
		row.info = sessionInfo(s);
		row.badge = s.crashed ? "crashed" : "";
		menu_sessions.push_back(std::move(row));
	}
	menu_no_sessions = sessions_.empty();
	menu_cls_more = menuSel_ == 5 ? "sel" : "";
	menu_more_count = sessions_.size() > shown ? std::format("{} total", sessions_.size()) : "";

	if (lanFocus_) {
		menu_hint = "enter connect · esc cancel";
	} else {
		menu_hint = "j/k move · enter open · q quit · ? keys";
	}
}

void App::beginLanEdit() {
	lanFocus_ = true;
	if (source_ == Source::Localhost) {
		stopSource();
		source_ = Source::None;
		link_ = Link::Searching;
	}
}

void App::activateMenu(int item) {
	switch (item) {
		case 0:
			if (source_ == Source::Localhost && link_ == Link::Connected) {
				enterMain();
			} else {
				menuAuto_ = true;
				startLocalhost();
			}
			break;
		case 1: beginLanEdit(); break;
		case 5: openOverlay(Overlay::Sessions); break;
		default: {
			const auto index = static_cast<std::size_t>(item - 2);
			if (index < sessions_.size()) {
				openSession(sessions_[index]);
			}
		}
	}
}

void App::openSession(const SessionInfo& session) {
	closeOverlay();
	startFile(session.path);
}

void App::MenuPick(std::string index) {
	consumed_ = true;
	menuSel_ = std::atoi(index.c_str());
	activateMenu(menuSel_);
}

void App::MenuSession(std::string index) {
	consumed_ = true;
	menuSel_ = 2 + std::atoi(index.c_str());
	activateMenu(menuSel_);
}

bool App::handleMenuKey(const Key& key) {
	using K = rtxui::Event::Keyboard;

	std::vector<int> items = {0, 1};
	for (std::size_t i = 0; i < std::min<std::size_t>(3, sessions_.size()); ++i) {
		items.push_back(static_cast<int>(2 + i));
	}
	items.push_back(5);
	const auto at = std::find(items.begin(), items.end(), menuSel_);
	const auto index = at == items.end() ? 0 : static_cast<int>(at - items.begin());

	if (key.is(K::Tab)) {
		beginBackgroundChange();
	} else if (key.is('j') || key.is(K::ArrowDown) || key.isCtrl('n')) {
		menuSel_ = items[static_cast<std::size_t>((index + 1) % static_cast<int>(items.size()))];
	} else if (key.is('k') || key.is(K::ArrowUp) || key.is(K::TabReverse) || key.isCtrl('p')) {
		menuSel_ = items[static_cast<std::size_t>((index + static_cast<int>(items.size()) - 1) % static_cast<int>(items.size()))];
	} else if (key.is('g') || key.is(K::Home)) {
		menuSel_ = 0;
	} else if (key.is('G') || key.is(K::End)) {
		menuSel_ = 5;
	} else if (key.ch >= '1' && key.ch <= '3' && !key.ctrl && !key.alt) {
		const auto recent = static_cast<std::size_t>(key.ch - '1');
		if (recent < std::min<std::size_t>(3, sessions_.size())) {
			menuSel_ = static_cast<int>(2 + recent);
			activateMenu(menuSel_);
		}
	} else if (key.is('/')) {
		activateMenu(5);
	} else if (key.is(K::Return) || key.is('l') || key.is(' ') || key.is(K::ArrowRight)) {
		activateMenu(menuSel_);
	} else if (key.is('i') && menuSel_ == 1) {
		beginLanEdit();
	} else if (key.is(K::Escape)) {
		if (source_ == Source::Lan) {
			menuAuto_ = true;
			startLocalhost();
		}
	} else if (key.is('r')) {
		refreshSessions();
	} else if (key.is('?')) {
		openOverlay(Overlay::Help);
	} else if (key.is('q')) {
		if (quit_) {
			quit_();
		}
	} else {
		return false;
	}
	return true;
}

bool App::sessSearchChanged() {
	sessSel_ = 0;
	sessTop_ = 0;
	return true;
}

void App::buildSessionsDialog() {
	sessList_.clear();
	const std::string needle = [&] {
		std::string s = sessSearch_.text();
		for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return s;
	}();
	for (std::size_t i = 0; i < sessions_.size(); ++i) {
		const SessionInfo& s = sessions_[i];
		if (!needle.empty() && !icontains(s.path.filename().string(), needle) && !icontains(fmt::relativeTime(s.start), needle)) {
			continue;
		}
		sessList_.push_back(i);
	}
	sess_empty = sessList_.empty();
	sessions_summary = std::format("{} found", sessions_.size());

	const std::size_t visible = static_cast<std::size_t>(std::clamp(rowsTotal_ - 12, 3, 16));
	sess_list_h = static_cast<int>(std::max<std::size_t>(1, std::min(visible, std::max<std::size_t>(sessList_.size(), 1))));
	const std::size_t h = static_cast<std::size_t>(sess_list_h);
	if (!sessList_.empty()) {
		sessSel_ = std::min(sessSel_, sessList_.size() - 1);
	}
	if (sessSel_ < sessTop_) sessTop_ = sessSel_;
	if (sessSel_ >= sessTop_ + h) sessTop_ = sessSel_ + 1 - h;

	std::vector<SessionRow> built;
	for (std::size_t i = sessTop_; i < sessList_.size() && i < sessTop_ + h; ++i) {
		const SessionInfo& s = sessions_[sessList_[i]];
		SessionRow row;
		row.cls = i == sessSel_ ? "sel" : "";
		row.when = fmt::relativeTime(s.start);
		row.name = s.path.filename().string();
		row.badge = s.crashed ? "crashed" : "";
		row.info = sessionInfo(s);
		built.push_back(std::move(row));
	}
	sess_rows = std::move(built);

	sess_search_cls = sessSearchFocus_ ? "on" : "";
	sess_before = sessSearchFocus_ ? sessSearch_.before() : sessSearch_.text();
	sess_under = sessSearchFocus_ ? sessSearch_.under() : "";
	sess_after = sessSearchFocus_ ? sessSearch_.after() : "";
	sess_cursor_cls = sessSearchFocus_ ? "cursor" : "";
	sess_placeholder = sessSearch_.empty() ? "filter sessions" : "";
}

bool App::handleSessionsKey(const Key& key) {
	using K = rtxui::Event::Keyboard;
	const std::size_t n = sessList_.size();
	const auto move = [&](long d) {
		if (n > 0) {
			sessSel_ = static_cast<std::size_t>(std::clamp<long>(static_cast<long>(sessSel_) + d, 0, static_cast<long>(n) - 1));
		}
	};
	if (key.is(K::Escape) || key.is('q')) {
		closeOverlay();
	} else if (key.is('j') || key.is(K::ArrowDown)) {
		move(1);
	} else if (key.is('k') || key.is(K::ArrowUp)) {
		move(-1);
	} else if (key.isCtrl('d') || key.is(K::PageDown)) {
		move(static_cast<long>(sess_list_h) / 2 + 1);
	} else if (key.isCtrl('u') || key.is(K::PageUp)) {
		move(-(static_cast<long>(sess_list_h) / 2 + 1));
	} else if (key.is('g') || key.is(K::Home)) {
		sessSel_ = 0;
	} else if (key.is('G') || key.is(K::End)) {
		move(1'000'000);
	} else if (key.is('/')) {
		sessSearchFocus_ = true;
	} else if (key.is(K::Return) || key.is('l') || key.is(K::ArrowRight)) {
		if (sessSel_ < n) {
			openSession(sessions_[sessList_[sessSel_]]);
		}
	} else {
		return false;
	}
	return true;
}

void App::SessRowClick(std::string index) {
	consumed_ = true;
	const std::size_t i = sessTop_ + static_cast<std::size_t>(std::atoi(index.c_str()));
	if (i < sessList_.size()) {
		openSession(sessions_[sessList_[i]]);
	}
}

void App::FocusSessSearch() {
	consumed_ = true;
	sessSearchFocus_ = true;
}

}  // namespace kenzo::ui
