#include <algorithm>
#include <cctype>
#include <format>

#include "core/format.hpp"
#include "ui/app.hpp"

namespace kenzo::ui {
namespace {

std::uint8_t groupBits(int group) {
	return group == 3 ? static_cast<std::uint8_t>((1u << kError) | (1u << kCritical)) : static_cast<std::uint8_t>(1u << group);
}

constexpr std::array<const char*, 4> kGroupNames = {"Trace", "Info", "Warning", "Error"};
constexpr std::array<const char*, 4> kGroupColors = {"c-t", "c-i", "c-w", "c-e"};
constexpr std::array<const char*, 4> kChipClass = {"t", "i", "w", "e"};
constexpr std::array<const char*, 4> kChipLabel = {"TRC", "INF", "WRN", "ERR"};

std::string lower(std::string s) {
	for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

struct Mark {
	std::string glyph, cls;
};

Mark markFor(std::uint8_t bits, std::uint8_t all) {
	bits &= all;
	if (bits == all) return {"◉", "on"};
	if (bits == 0) return {"○", ""};
	return {"◐", "part"};
}

}  // namespace

void App::openOverlay(Overlay which) {
	overlay_ = which;
	switch (which) {
		case Overlay::Severities: sevSel_ = 0; break;
		case Overlay::Sinks:
			sinkSel_ = 0;
			sinkTop_ = 0;
			sinkSearchFocus_ = false;
			break;
		case Overlay::Sessions:
			sessSel_ = 0;
			sessTop_ = 0;
			sessSearchFocus_ = false;
			refreshSessions();
			break;
		case Overlay::Remote:
			remoteSel_ = 0;
			remoteTop_ = 0;
			remoteSearchFocus_ = false;
			remoteSearch_.clear();
			passphrase_.clear();
			remoteMessage_.clear();
			remoteDownloading_ = false;
			remote_->open();
			break;
		case Overlay::Help: helpTop_ = 0; break;
		default: break;
	}
}

void App::closeOverlay() {
	if (overlay_ == Overlay::Remote) {
		remote_->cancelDownload();
		if (remote_->snapshot().state == RemoteClient::State::NeedPassphrase) {
			remote_->cancelPassphrase();
		}
		passphrase_.clear();
		remoteSearchFocus_ = false;
	}
	overlay_ = Overlay::None;
	sinkSearchFocus_ = false;
	sessSearchFocus_ = false;
	ctxInSub_ = false;
	ctxCopySub_ = false;
	show_ctx_sub = false;
	chord_ = 0;
}

void App::buildSeverities() {
	const kenzo::Data& data = *store_.data();
	const FilterState& f = index_->filter();
	std::vector<SevRow> built;
	for (int g = 0; g < 4; ++g) {
		SevRow row;
		const auto mark = markFor(f.sevMask, groupBits(g));
		const auto bits = static_cast<std::uint8_t>(f.sevMask & groupBits(g));
		const std::uint8_t all = groupBits(g);
		const auto m = markFor(bits, all);
		(void)mark;
		row.cls = std::string(static_cast<std::size_t>(g) == sevSel_ ? "sel" : "") + (bits == 0 ? " off" : "");
		row.chk = m.glyph;
		row.chk_cls = m.cls;
		row.color = kGroupColors[static_cast<std::size_t>(g)];
		row.name = kGroupNames[static_cast<std::size_t>(g)];
		const std::uint64_t count = g == 3 ? data.severityCount(kError) + data.severityCount(kCritical) : data.severityCount(g);
		row.count = std::to_string(count);
		row.key = std::string();
		built.push_back(std::move(row));
	}
	sev_rows = std::move(built);
}

bool App::handleSeverityKey(const Key& key) {
	using K = rtxui::Event::Keyboard;
	if (key.is(K::Escape) || key.is('q') || key.is('f')) {
		closeOverlay();
	} else if (key.is('j') || key.is(K::ArrowDown)) {
		sevSel_ = std::min<std::size_t>(3, sevSel_ + 1);
	} else if (key.is('k') || key.is(K::ArrowUp)) {
		sevSel_ = sevSel_ ? sevSel_ - 1 : 0;
	} else if (key.is(' ') || key.is(K::Return)) {
		toggleGroup(static_cast<int>(sevSel_));
	} else if (key.ch >= '1' && key.ch <= '4' && !key.ctrl) {
		toggleGroup(static_cast<int>(key.ch - '1'));
	} else if (key.is('a')) {
		FilterState next = index_->filter();
		next.sevMask = FilterState::kAll;
		applyFilter(std::move(next));
	} else if (key.is('i')) {
		FilterState next = index_->filter();
		next.sevMask = static_cast<std::uint8_t>(~next.sevMask & FilterState::kAll);
		applyFilter(std::move(next));
	} else {
		return false;
	}
	return true;
}

void App::SevRowClick(std::string index) {
	consumed_ = true;
	sevSel_ = static_cast<std::size_t>(std::clamp(std::atoi(index.c_str()), 0, 3));
	toggleGroup(static_cast<int>(sevSel_));
}

std::vector<StrId> App::visibleSinks() const {
	const kenzo::Data& data = *store_.data();
	const std::string needle = lower(sinkSearch_.text());
	std::vector<StrId> out;
	const auto& counts = data.sinkCounts();
	for (StrId id = 0; id < data.sinks().size(); ++id) {
		if (id == 0 && (counts.empty() || std::all_of(counts[0].begin(), counts[0].end(), [](auto c) { return c == 0; }))) {
			continue;
		}
		if (!needle.empty() && !icontains(data.sinks().get(id), needle)) {
			continue;
		}
		out.push_back(id);
	}
	std::ranges::sort(out, [&](StrId a, StrId b) { return lower(std::string(data.sinks().get(a))) < lower(std::string(data.sinks().get(b))); });
	return out;
}

bool App::sinkSearchChanged() {
	sinkSel_ = 0;
	sinkTop_ = 0;
	return true;
}

void App::buildSinks() {
	const kenzo::Data& data = *store_.data();
	const FilterState& f = index_->filter();
	sinkList_ = visibleSinks();

	const std::size_t visible = static_cast<std::size_t>(std::clamp(rowsTotal_ - 12, 3, 18));
	sinks_list_h = static_cast<int>(std::max<std::size_t>(1, std::min(visible, std::max<std::size_t>(sinkList_.size(), 1))));
	const auto h = static_cast<std::size_t>(sinks_list_h);
	if (!sinkList_.empty()) {
		sinkSel_ = std::min(sinkSel_, sinkList_.size() - 1);
	}
	if (sinkSel_ < sinkTop_) sinkTop_ = sinkSel_;
	if (sinkSel_ >= sinkTop_ + h) sinkTop_ = sinkSel_ + 1 - h;

	std::size_t hidden = 0;
	for (StrId id = 1; id < data.sinks().size(); ++id) {
		if (f.bitsFor(id) == 0) ++hidden;
	}
	sinks_summary = std::format("{} sinks{}", data.sinks().size() > 0 ? data.sinks().size() - 1 : 0,
	                            hidden ? std::format(" · {} hidden", hidden) : std::string());

	std::vector<SinkRow> built;
	for (std::size_t i = sinkTop_; i < sinkList_.size() && i < sinkTop_ + h; ++i) {
		const StrId id = sinkList_[i];
		const std::uint8_t bits = f.bitsFor(id);
		SinkRow row;
		row.cls = std::string(i == sinkSel_ ? "sel" : "") + (bits == 0 ? " off" : "");
		const auto mark = markFor(bits, FilterState::kAll);
		row.chk = mark.glyph;
		row.chk_cls = mark.cls;
		const std::string_view name = data.sinks().get(id);
		row.name = name.empty() ? "(no sink)" : std::string(name);
		std::uint64_t total = 0;
		if (id < data.sinkCounts().size()) {
			for (auto c : data.sinkCounts()[id]) total += c;
		}
		row.count = std::to_string(total);
		std::string* classes[4] = {&row.k0, &row.k1, &row.k2, &row.k3};
		std::string* labels[4] = {&row.l0, &row.l1, &row.l2, &row.l3};
		std::string* codes[4] = {&row.i0, &row.i1, &row.i2, &row.i3};
		for (int g = 0; g < 4; ++g) {
			const bool on = (bits & groupBits(g)) == groupBits(g);
			*classes[g] = std::string(kChipClass[static_cast<std::size_t>(g)]) + (on ? " on" : "");
			*labels[g] = kChipLabel[static_cast<std::size_t>(g)];
			*codes[g] = std::to_string(id * 4 + static_cast<StrId>(g));
		}
		built.push_back(std::move(row));
	}
	sink_rows = std::move(built);
	sinks_empty = sinkList_.empty();

	sinks_search_cls = sinkSearchFocus_ ? "on" : "";
	sinks_before = sinkSearchFocus_ ? sinkSearch_.before() : sinkSearch_.text();
	sinks_under = sinkSearchFocus_ ? sinkSearch_.under() : "";
	sinks_after = sinkSearchFocus_ ? sinkSearch_.after() : "";
	sinks_cursor_cls = sinkSearchFocus_ ? "cursor" : "";
	sinks_placeholder = sinkSearch_.empty() ? "filter sinks" : "";
}

void App::toggleSinkRow(std::size_t listIndex) {
	if (listIndex >= sinkList_.size()) return;
	const StrId id = sinkList_[listIndex];
	FilterState next = index_->filter();
	next.setSinkBits(id, next.bitsFor(id) == 0 ? FilterState::kAll : std::uint8_t {0});
	applyFilter(std::move(next));
}

void App::toggleSinkGroup(StrId sink, int group) {
	FilterState next = index_->filter();
	std::uint8_t bits = next.bitsFor(sink);
	const std::uint8_t g = groupBits(group);
	bits = (bits & g) == g ? static_cast<std::uint8_t>(bits & ~g) : static_cast<std::uint8_t>(bits | g);
	next.setSinkBits(sink, bits);
	applyFilter(std::move(next));
}

void App::invertSink(StrId sink) {
	FilterState next = index_->filter();
	next.setSinkBits(sink, static_cast<std::uint8_t>(~next.bitsFor(sink) & FilterState::kAll));
	applyFilter(std::move(next));
}

bool App::handleSinkKey(const Key& key) {
	using K = rtxui::Event::Keyboard;
	const std::size_t n = sinkList_.size();
	const auto move = [&](long d) {
		if (n > 0) {
			sinkSel_ = static_cast<std::size_t>(std::clamp<long>(static_cast<long>(sinkSel_) + d, 0, static_cast<long>(n) - 1));
		}
	};
	if (key.is(K::Escape) || key.is('q')) {
		closeOverlay();
	} else if (key.is('j') || key.is(K::ArrowDown)) {
		move(1);
	} else if (key.is('k') || key.is(K::ArrowUp)) {
		move(-1);
	} else if (key.isCtrl('d') || key.is(K::PageDown)) {
		move(static_cast<long>(sinks_list_h) / 2 + 1);
	} else if (key.isCtrl('u') || key.is(K::PageUp)) {
		move(-(static_cast<long>(sinks_list_h) / 2 + 1));
	} else if (key.is('g') || key.is(K::Home)) {
		sinkSel_ = 0;
	} else if (key.is('G') || key.is(K::End)) {
		move(1'000'000);
	} else if (key.is('/')) {
		sinkSearchFocus_ = true;
	} else if (key.is(' ') || key.is(K::Return)) {
		toggleSinkRow(sinkSel_);
	} else if (key.ch >= '1' && key.ch <= '4' && !key.ctrl) {
		if (sinkSel_ < n) toggleSinkGroup(sinkList_[sinkSel_], static_cast<int>(key.ch - '1'));
	} else if (key.is('i')) {
		if (sinkSel_ < n) invertSink(sinkList_[sinkSel_]);
	} else if (key.is('I')) {
		FilterState next = index_->filter();
		for (StrId id : sinkList_) {
			next.setSinkBits(id, static_cast<std::uint8_t>(~next.bitsFor(id) & FilterState::kAll));
		}
		applyFilter(std::move(next));
	} else if (key.is('a')) {
		FilterState next = index_->filter();
		for (StrId id : sinkList_) next.setSinkBits(id, FilterState::kAll);
		applyFilter(std::move(next));
	} else if (key.is('x')) {
		FilterState next = index_->filter();
		for (StrId id : sinkList_) next.setSinkBits(id, 0);
		applyFilter(std::move(next));
	} else if (key.is('u')) {
		undoLast();
	} else if (key.is('o') || (key.ch == ' ' && key.ctrl)) {
		if (sinkSel_ < n) soloSink(sinkList_[sinkSel_]);
	} else {
		return false;
	}
	return true;
}

void App::soloSink(StrId sink) {
	FilterState next = index_->filter();
	const kenzo::Data& data = *store_.data();
	bool alone = next.bitsFor(sink) != 0;
	for (StrId id = 0; id < data.sinks().size() && alone; ++id) {
		if (id == 0 && std::ranges::all_of(data.sinkCounts()[0], [](auto count) { return count == 0; })) continue;
		alone = id == sink || next.bitsFor(id) == 0;
	}
	for (StrId id = 0; id < data.sinks().size(); ++id) {
		next.setSinkBits(id, alone || id == sink ? FilterState::kAll : std::uint8_t {0});
	}
	applyFilter(std::move(next));
}

void App::SinkRowClick(std::string index) {
	consumed_ = true;
	const std::size_t i = sinkTop_ + static_cast<std::size_t>(std::atoi(index.c_str()));
	if (i < sinkList_.size()) sinkSel_ = i;
}

void App::SinkToggle(std::string index) {
	consumed_ = true;
	const std::size_t i = sinkTop_ + static_cast<std::size_t>(std::atoi(index.c_str()));
	if (i < sinkList_.size()) {
		sinkSel_ = i;
		toggleSinkRow(i);
	}
}

void App::SinkChip(std::string code) {
	consumed_ = true;
	const auto value = static_cast<StrId>(std::strtoul(code.c_str(), nullptr, 10));
	toggleSinkGroup(value / 4, static_cast<int>(value % 4));
}

void App::FocusSinkSearch() {
	consumed_ = true;
	sinkSearchFocus_ = true;
}

void App::buildHelp() {
	static const std::vector<HelpRow> all = {
	    {"", "Moving", "head"},
	    {"j k  ↓ ↑", "next / previous log", ""},
	    {"ctrl-d ctrl-u", "half page down / up", ""},
	    {"ctrl-f ctrl-b", "page down / up   (also PgDn PgUp)", ""},
	    {"gg  G", "first / last log (G also resumes follow)", ""},
	    {"H M L", "top / middle / bottom of the screen", ""},
	    {"zz zt zb", "center / top / bottom the selection", ""},
	    {"e  E", "next / previous error", ""},
	    {"", "", ""},
	    {"", "Logs", "head"},
	    {"enter  o  l", "open details", ""},
	    {"space  m", "context menu (also right-click): open, copy, hide", ""},
	    {"yy  ym  yb", "copy entry / message / backtrace", ""},
	    {"hf  hh  hs", "hide this severity / this sink / this severity on this sink", ""},
	    {"/", "search: words, \"phrases\", -exclude, sink: file: msg: sev: bt: time:", ""},
	    {"1 2 3 4", "toggle trace / info / warning / error", ""},
	    {"f  s", "severities popup · sinks popup", ""},
	    {"u", "undo the last filter change or clear", ""},
	    {"X  F  t", "clear all · follow on/off · absolute/relative time", ""},
	    {"q  ctrl-c", "back to the menu · quit", ""},
	    {"", "", ""},
	    {"", "Popups and details", "head"},
	    {"j k  space", "move · toggle", ""},
	    {"1-4  i  I  a  x", "sinks: level for this sink · invert it · invert all · all on · all off", ""},
	    {"o  ctrl-space", "sinks: show only this sink (again to show all)", ""},
	    {"h l  tab", "details: switch between message and backtrace", ""},
	    {"[  ]", "details: previous / next log", ""},
	    {"esc  q", "close", ""},
	};
	const std::size_t visible = static_cast<std::size_t>(std::clamp(rowsTotal_ - 8, 5, 30));
	const std::size_t h = std::min(visible, all.size());
	help_h = static_cast<int>(h);
	helpTop_ = std::min(helpTop_, all.size() - h);
	help_rows.assign(all.begin() + static_cast<long>(helpTop_), all.begin() + static_cast<long>(helpTop_ + h));
}

bool App::handleHelpKey(const Key& key) {
	using K = rtxui::Event::Keyboard;
	if (key.is(K::Escape) || key.is('q') || key.is('?')) {
		closeOverlay();
	} else if (key.is('j') || key.is(K::ArrowDown)) {
		++helpTop_;
	} else if (key.is('k') || key.is(K::ArrowUp)) {
		helpTop_ = helpTop_ ? helpTop_ - 1 : 0;
	} else {
		return false;
	}
	return true;
}

void App::openContext(std::size_t position, int x, int y) {
	if (position >= index_->size()) return;
	ctxPosition_ = (*index_)[position];
	ctxSel_ = 0;
	ctxSubSel_ = 0;
	ctxInSub_ = false;
	ctxCopySub_ = false;
	show_ctx_sub = false;
	ctx_x = std::clamp(x, 0, std::max(0, cols_ - ctxW_ - 1));
	ctx_y = std::clamp(y, 0, std::max(0, rowsTotal_ - 5));
	overlay_ = Overlay::Context;
}

void App::buildContext() {
	if (ctxPosition_ >= store_.size()) {
		closeOverlay();
		return;
	}
	const LogRecord& rec = store_[ctxPosition_];
	const kenzo::Data& data = *store_.data();
	const bool hasBacktrace = rec.backtrace != 0;
	const auto item = [&](std::size_t i, const char* label, const char* key, bool submenu = false) {
		return CtxItem {!ctxInSub_ && ctxSel_ == i ? "sel" : "", label, key, submenu ? "▸" : ""};
	};
	ctx_items = {item(0, "Open details", "o"), item(1, "Copy", "y", true), item(2, "Hide", "h", true)};

	const std::string sev(severityName(rec.sev));
	const std::string sink = std::string(data.sinks().get(rec.sink));
	const std::string sinkName = sink.empty() ? "(no sink)" : sink;
	const auto sub = [&](std::size_t i, std::string label, const char* key, bool enabled = true) {
		return CtxItem {std::string(ctxInSub_ && ctxSubSel_ == i ? "sel" : "") + (enabled ? "" : " dis"), std::move(label), key, ""};
	};
	if (ctxCopySub_) {
		ctx_sub_items = {sub(0, "Copy entry", "yy"), sub(1, "Copy message", "ym"), sub(2, "Copy backtrace", "yb", hasBacktrace)};
	} else {
		ctx_sub_items = {sub(0, std::format("{} severity", sev), "hf"), sub(1, std::format("{} sink", sinkName), "hh"),
		                 sub(2, std::format("{} on {}", sev, sinkName), "hs")};
	}

	ctx_sub_x = ctx_x + ctxW_ - 1;
	if (ctx_sub_x + 34 > cols_) ctx_sub_x = std::max(0, ctx_x - 33);
	ctx_sub_y = std::min(ctx_y + static_cast<int>(ctxSel_), std::max(0, rowsTotal_ - 5));
	show_ctx_sub = ctxInSub_;
}

void App::runContext(int item) {
	if (ctxPosition_ >= store_.size()) {
		closeOverlay();
		return;
	}
	switch (item) {
		case 0: {
			const std::size_t pos = index_->lowerBound(static_cast<std::uint32_t>(ctxPosition_));
			closeOverlay();
			openDetail(pos);
			break;
		}
		case 1:
		case 2:
			ctxSel_ = static_cast<std::size_t>(item);
			ctxInSub_ = true;
			ctxCopySub_ = item == 1;
			ctxSubSel_ = 0;
			break;
		default: break;
	}
}

void App::runContextSub(int item) {
	if (ctxPosition_ >= store_.size()) {
		closeOverlay();
		return;
	}
	const LogRecord& rec = store_[ctxPosition_];
	if (overlay_ == Overlay::Context && ctxCopySub_) {
		switch (item) {
			case 0: copyText(recordText(rec, true), "log entry"); break;
			case 1: copyText(rec.message, "message"); break;
			case 2:
				if (rec.backtrace == 0) toast("This log has no backtrace", "warn");
				else copyText(std::string(store_.data()->backtraces().get(rec.backtrace)), "backtrace");
				break;
			default: return;
		}
		closeOverlay();
		return;
	}
	const kenzo::Data& data = *store_.data();
	const std::string sev(severityName(rec.sev));
	const std::string sink = std::string(data.sinks().get(rec.sink));
	const std::string sinkName = sink.empty() ? "(no sink)" : sink;
	const auto sevBit = static_cast<std::uint8_t>(1u << rec.sev);

	FilterState next = index_->filter();
	std::string label;
	switch (item) {
		case 0:
			next.sevMask &= static_cast<std::uint8_t>(~sevBit);
			label = std::format("Hidden {}", sev);
			break;
		case 1:
			next.setSinkBits(rec.sink, 0);
			label = std::format("Hidden sink {}", sinkName);
			break;
		case 2:
			next.setSinkBits(rec.sink, static_cast<std::uint8_t>(next.bitsFor(rec.sink) & ~sevBit));
			label = std::format("Hidden {} on {}", sev, sinkName);
			break;
		default: return;
	}
	closeOverlay();
	applyFilter(std::move(next), label.c_str());
}

bool App::handleContextKey(const Key& key) {
	using K = rtxui::Event::Keyboard;
	if (ctxInSub_) {
		if (key.is(K::Escape) || key.is(K::ArrowLeft) || key.is('q') || (ctxCopySub_ && key.is('h'))) {
			ctxInSub_ = false;
		} else if (ctxCopySub_ && (key.is('y') || key.is('m') || key.is('b'))) {
			runContextSub(key.is('y') ? 0 : key.is('m') ? 1 : 2);
		} else if (!ctxCopySub_ && (key.is('f') || key.is('h') || key.is('s'))) {
			runContextSub(key.is('f') ? 0 : key.is('h') ? 1 : 2);
		} else if (key.is('j') || key.is(K::ArrowDown)) {
			ctxSubSel_ = std::min<std::size_t>(2, ctxSubSel_ + 1);
		} else if (key.is('k') || key.is(K::ArrowUp)) {
			ctxSubSel_ = ctxSubSel_ ? ctxSubSel_ - 1 : 0;
		} else if (key.is(K::Return) || key.is('l') || key.is(' ') || key.is(K::ArrowRight)) {
			runContextSub(static_cast<int>(ctxSubSel_));
		} else {
			return false;
		}
		return true;
	}
	if (key.is(K::Escape) || key.is('q') || key.is('m')) {
		closeOverlay();
	} else if (key.is('j') || key.is(K::ArrowDown)) {
		ctxSel_ = std::min<std::size_t>(2, ctxSel_ + 1);
	} else if (key.is('k') || key.is(K::ArrowUp)) {
		ctxSel_ = ctxSel_ ? ctxSel_ - 1 : 0;
	} else if (key.is(K::Return) || key.is('l') || key.is(' ') || key.is(K::ArrowRight)) {
		runContext(static_cast<int>(ctxSel_));
	} else if (key.is('o')) {
		runContext(0);
	} else if (key.is('h')) {
		runContext(2);
	} else if (key.is('y')) {
		runContext(1);
	} else {
		return false;
	}
	return true;
}

void App::CtxPick(std::string index) {
	consumed_ = true;
	ctxSel_ = static_cast<std::size_t>(std::clamp(std::atoi(index.c_str()), 0, 2));
	runContext(static_cast<int>(ctxSel_));
}

void App::CtxSubPick(std::string index) {
	consumed_ = true;
	runContextSub(std::clamp(std::atoi(index.c_str()), 0, 2));
}

}  // namespace kenzo::ui
