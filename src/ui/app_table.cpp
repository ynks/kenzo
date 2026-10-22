#include <algorithm>
#include <format>

#include "core/format.hpp"
#include "ui/app.hpp"

namespace kenzo::ui {
namespace {

constexpr std::array<const char*, 10> kSpinner = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
constexpr std::size_t kMaxLineBytes = 400;

std::uint8_t groupBits(int group) {
	return group == 3 ? static_cast<std::uint8_t>((1u << kError) | (1u << kCritical)) : static_cast<std::uint8_t>(1u << group);
}

std::string firstLine(const std::string& text, bool& more) {
	const std::size_t nl = text.find('\n');
	std::size_t end = std::min({nl == std::string::npos ? text.size() : nl, kMaxLineBytes});
	more = nl != std::string::npos || end < text.size();
	while (end > 0 && end < text.size() && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
		--end;
	}
	std::string line = text.substr(0, end);
	while (!line.empty() && line.back() == '\r') {
		line.pop_back();
	}
	for (char& c : line) {
		if (static_cast<unsigned char>(c) < 0x20) {
			c = ' ';
		}
	}
	return line;
}

std::array<std::string, 3> splitHit(const std::string& text, const std::vector<std::string_view>& needles) {
	std::size_t best = std::string::npos, bestLen = 0;
	for (std::string_view needle : needles) {
		const std::size_t at = ifind(text, needle);
		if (at != std::string::npos && (best == std::string::npos || at < best)) {
			best = at;
			bestLen = needle.size();
		}
	}
	if (best == std::string::npos) {
		return {text, {}, {}};
	}
	return {text.substr(0, best), text.substr(best, bestLen), text.substr(best + bestLen)};
}

std::array<std::string, 7> highlightSegments(const std::string& line, const std::vector<std::string_view>& needles) {
	std::array<std::string, 7> out;
	std::size_t cursor = 0;
	std::size_t slot = 0;
	while (slot < 6) {
		std::size_t best = std::string::npos, bestLen = 0;
		for (std::string_view needle : needles) {
			const std::size_t at = ifind(line, needle, cursor);
			if (at != std::string::npos && (best == std::string::npos || at < best)) {
				best = at;
				bestLen = needle.size();
			}
		}
		if (best == std::string::npos) {
			break;
		}
		out[slot] = line.substr(cursor, best - cursor);
		out[slot + 1] = line.substr(best, bestLen);
		cursor = best + bestLen;
		slot += 2;
	}
	out[slot] = line.substr(cursor);
	return out;
}

}  // namespace

std::size_t App::viewRows() const {
	return static_cast<std::size_t>(std::max(1, rowsTotal_ - 3));
}

const LogRecord* App::selected() const {
	if (index_->size() == 0 || sel_ >= index_->size()) {
		return nullptr;
	}
	return &store_[(*index_)[sel_]];
}

void App::followIfNeeded() {
	const std::size_t n = index_->size();
	const std::size_t h = viewRows();

	if (index_->epoch() != lastEpoch_) {
		lastEpoch_ = index_->epoch();
		if (follow_) {
			sel_ = n ? n - 1 : 0;
		} else if (haveSelRecord_) {
			sel_ = std::min(index_->lowerBound(static_cast<std::uint32_t>(selRecord_)), n ? n - 1 : 0);
		}
	} else if (follow_ && n != lastViewSize_) {
		sel_ = n ? n - 1 : 0;
	}
	lastViewSize_ = n;

	if (n == 0) {
		sel_ = 0;
		scrollTop_ = 0;
		haveSelRecord_ = false;
		return;
	}
	sel_ = std::min(sel_, n - 1);
	const std::size_t maxTop = n > h ? n - h : 0;
	if (follow_) {
		scrollTop_ = maxTop;
	} else {
		scrollTop_ = std::min(scrollTop_, maxTop);
	}
	selRecord_ = (*index_)[sel_];
	haveSelRecord_ = true;
}

void App::ensureVisible(bool center) {
	const std::size_t n = index_->size();
	const std::size_t h = viewRows();
	if (n == 0) {
		return;
	}
	const std::size_t maxTop = n > h ? n - h : 0;
	if (center) {
		scrollTop_ = sel_ > h / 2 ? std::min(maxTop, sel_ - h / 2) : 0;
	} else if (sel_ < scrollTop_) {
		scrollTop_ = sel_;
	} else if (sel_ >= scrollTop_ + h) {
		scrollTop_ = sel_ - h + 1;
	}
	scrollTop_ = std::min(scrollTop_, maxTop);
}

void App::setSel(std::size_t position) {
	const std::size_t n = index_->size();
	if (n == 0) {
		return;
	}
	sel_ = std::min(position, n - 1);
	follow_ = sel_ == n - 1;
	ensureVisible();
}

void App::moveSel(long delta) {
	const std::size_t n = index_->size();
	if (n == 0) {
		return;
	}
	const long target = std::clamp<long>(static_cast<long>(sel_) + delta, 0, static_cast<long>(n) - 1);
	setSel(static_cast<std::size_t>(target));
}

void App::scrollBy(long delta) {
	const std::size_t n = index_->size();
	const std::size_t h = viewRows();
	if (n == 0) {
		return;
	}
	const std::size_t maxTop = n > h ? n - h : 0;
	scrollTop_ = static_cast<std::size_t>(std::clamp<long>(static_cast<long>(scrollTop_) + delta, 0, static_cast<long>(maxTop)));
	sel_ = std::clamp(sel_, scrollTop_, std::min(n - 1, scrollTop_ + h - 1));
	follow_ = scrollTop_ == maxTop && sel_ == n - 1;
}

void App::goBottom() {
	const std::size_t n = index_->size();
	follow_ = true;
	if (n == 0) {
		return;
	}
	sel_ = n - 1;
	const std::size_t h = viewRows();
	scrollTop_ = n > h ? n - h : 0;
}

void App::jumpToError(int direction) {
	const auto& hot = index_->hot();
	const std::size_t n = index_->size();
	if (hot.empty() || n == 0) {
		toast("No errors in view", "warn");
		return;
	}
	if (direction > 0) {
		auto it = std::upper_bound(hot.begin(), hot.end(), static_cast<std::uint32_t>(sel_),
		                           [](std::uint32_t pos, const auto& entry) { return pos < entry.first; });
		for (; it != hot.end(); ++it) {
			if (it->second >= kError) {
				setSel(it->first);
				ensureVisible(true);
				return;
			}
		}
	} else {
		auto it = std::lower_bound(hot.begin(), hot.end(), static_cast<std::uint32_t>(sel_),
		                           [](const auto& entry, std::uint32_t pos) { return entry.first < pos; });
		while (it != hot.begin()) {
			--it;
			if (it->second >= kError) {
				setSel(it->first);
				ensureVisible(true);
				return;
			}
		}
	}
	toast(direction > 0 ? "No more errors below" : "No more errors above", "warn");
}

void App::buildRows() {
	const kenzo::Data& data = *store_.data();
	const std::size_t n = index_->size();
	const std::size_t h = viewRows();

	std::size_t sinkLen = 4;
	for (std::size_t i = 1; i < data.sinks().size(); ++i) {
		sinkLen = std::max(sinkLen, data.sinks().get(static_cast<StrId>(i)).size());
	}
	sink_w = static_cast<int>(std::min<std::size_t>(sinkLen, 18)) + 1;
	file_w = cols_ >= 150 ? 28 : 22;
	time_header = relativeTime_ ? "Offset" : "Time";

	const auto needles = index_->query().highlights(Query::Field::Message);
	const auto sinkNeedles = index_->query().highlights(Query::Field::Sink);
	const auto fileNeedles = index_->query().highlights(Query::Field::File);
	const auto timeNeedles = relativeTime_ ? std::vector<std::string_view> {} : index_->query().highlights(Query::Field::Time);
	std::vector<LogRow> built;
	built.reserve(h);
	for (std::size_t i = 0; i < h && scrollTop_ + i < n; ++i) {
		const std::size_t pos = scrollTop_ + i;
		const LogRecord& rec = store_[(*index_)[pos]];
		LogRow row;
		row.cls = std::format("sev{}{}", rec.sev, pos == sel_ ? " sel" : "");
		row.ptr = pos == sel_ ? "›" : " ";
		row.bar = rec.sev == kTrace ? " " : "▎";
		row.tag = std::string(severityShort(rec.sev));
		auto time = splitHit(relativeTime_ ? fmt::offsetTime(rec.ts, firstTs_) : fmt::clockTime(rec.ts), timeNeedles);
		row.t0 = std::move(time[0]);
		row.t1 = std::move(time[1]);
		row.t2 = std::move(time[2]);
		auto sink = splitHit(std::string(data.sinks().get(rec.sink)), sinkNeedles);
		row.sk0 = std::move(sink[0]);
		row.sk1 = std::move(sink[1]);
		row.sk2 = std::move(sink[2]);
		const auto file = data.files().get(rec.file);
		auto fileParts = splitHit(file.empty() ? std::string() : std::format("{}:{}", fmt::baseName(file), rec.line), fileNeedles);
		row.f0 = std::move(fileParts[0]);
		row.f1 = std::move(fileParts[1]);
		row.f2 = std::move(fileParts[2]);

		bool more = false;
		const std::string line = firstLine(rec.message, more);
		auto seg = needles.empty() ? std::array<std::string, 7> {line} : highlightSegments(line, needles);
		row.m0 = std::move(seg[0]);
		row.m1 = std::move(seg[1]);
		row.m2 = std::move(seg[2]);
		row.m3 = std::move(seg[3]);
		row.m4 = std::move(seg[4]);
		row.m5 = std::move(seg[5]);
		row.m6 = std::move(seg[6]);
		row.more = (more || !rec.details.empty()) ? "…" : "";
		built.push_back(std::move(row));
	}
	rows = std::move(built);

	rows_empty = n == 0;
	if (rows_empty) {
		const bool filtered = !index_->query().empty() || !(index_->filter() == FilterState {});
		if (store_.size() > 0 && filtered) {
			empty_text = "Nothing matches the current search and filters.";
		} else if (source_ == Source::File) {
			empty_text = loadProgress_ < 1.0 ? "Reading file…" : "This file has no log records.";
		} else if (link_ == Link::Connected) {
			empty_text = "Connected. Waiting for logs…";
		} else {
			empty_text = "Waiting for the engine…";
		}
	}
}

void App::buildBar() {
	const std::size_t n = index_->size();
	const std::size_t h = viewRows();
	std::vector<BarCell> cells(h);
	if (n > h) {
		const double scale = static_cast<double>(h) / static_cast<double>(n);
		const auto thumbTop = static_cast<std::size_t>(static_cast<double>(scrollTop_) * scale);
		const auto thumbLen = std::max<std::size_t>(1, static_cast<std::size_t>(static_cast<double>(h) * scale + 0.5));
		const auto& hot = index_->hot();
		for (std::size_t c = 0; c < h; ++c) {
			std::string cls;
			if (c >= thumbTop && c < thumbTop + thumbLen) {
				cls = "th";
			}
			const auto lo = static_cast<std::uint32_t>(c * n / h);
			const auto hi = static_cast<std::uint32_t>((c + 1) * n / h);
			auto it = std::lower_bound(hot.begin(), hot.end(), lo, [](const auto& e, std::uint32_t v) { return e.first < v; });
			int worst = -1;
			for (int guard = 0; it != hot.end() && it->first < hi && guard < 64; ++it, ++guard) {
				worst = std::max<int>(worst, it->second);
			}
			if (worst >= kError) {
				cls += cls.empty() ? "me" : " me";
			} else if (worst == kWarning) {
				cls += cls.empty() ? "mw" : " mw";
			}
			cells[c].cls = std::move(cls);
		}
	}
	bar_cells = std::move(cells);
}

void App::buildStatus() {
	const kenzo::Data& data = *store_.data();
	const std::string spin = kSpinner[static_cast<std::size_t>(spinner_) % kSpinner.size()];

	switch (source_) {
		case Source::File:
			st_mode_cls = "p-magenta";
			st_mode = "FILE";
			st_conn_cls = "";
			st_conn = loadProgress_ < 1.0 ? std::format("{} {} loading {:.0f}%", spin, fileName_, loadProgress_ * 100)
			                              : fileName_;
			break;
		case Source::Localhost:
		case Source::Lan: {
			const bool lan = source_ == Source::Lan;
			if (link_ == Link::Connected) {
				st_mode_cls = lan ? "p-green" : "p-blue";
				st_mode = lan ? "LAN" : "LOCALHOST";
				st_conn_cls = "";
				st_conn = lan ? target_.display() : "";
			} else {
				st_mode_cls = "p-red";
				st_mode = "DISCONNECTED";
				st_conn_cls = "bad";
				st_conn = std::format("{} {}…", spin, link_ == Link::Searching ? "searching " + target_.display() : "reconnecting to " + target_.display());
			}
			break;
		}
		case Source::None: break;
	}
	if (index_->rebuilding()) {
		st_conn += std::format("  {} filtering", spin);
	}

	const FilterState& f = index_->filter();
	const auto cls = [&](int g) { return (f.sevMask & groupBits(g)) == 0 ? std::string("off") : std::string(); };
	cnt_cls0 = cls(0);
	cnt_cls1 = cls(1);
	cnt_cls2 = cls(2);
	cnt_cls3 = cls(3);
	cnt0 = std::to_string(data.severityCount(kTrace));
	cnt1 = std::to_string(data.severityCount(kInfo));
	cnt2 = std::to_string(data.severityCount(kWarning));
	cnt3 = std::to_string(data.severityCount(kError) + data.severityCount(kCritical));

	follow_text = follow_ ? "● follow" : "○ paused";
	follow_cls = follow_ ? "on" : "";
	st_pending = chord_ ? std::string(1, chord_) + "…" : std::string();
	int levelsWidth = static_cast<int>(cnt0.size() + cnt1.size() + cnt2.size() + cnt3.size()) + 24;
	const bool shortLabels = levelsWidth > cols_;
	cnt_label0 = shortLabels ? "T" : "TRC";
	cnt_label1 = shortLabels ? "I" : "INF";
	cnt_label2 = shortLabels ? "W" : "WRN";
	cnt_label3 = shortLabels ? "E" : "ERR";
	if (shortLabels) levelsWidth -= 8;
	st_show_levels = levelsWidth <= cols_;
	const int countsWidth = st_show_levels ? levelsWidth : 0;
	st_show_follow = countsWidth + 10 <= cols_;
	const int rightWidth = countsWidth + (st_show_follow ? 10 : 0);
	const int leftWidth = static_cast<int>(st_mode.size() + st_conn.size()) + 4 + (chord_ ? 4 : 0);
	const auto fitsHints = [&](int width) {
		const int x = (cols_ - width) / 2;
		return x >= leftWidth && x + width <= cols_ - rightWidth;
	};
	const bool fullHints = fitsHints(46);
	st_show_hints = fullHints || fitsHints(27);
	st_hints_cls = fullHints ? "" : "compact";
	st_hint_x = (cols_ - (fullHints ? 46 : 27)) / 2;
}

void App::buildSearch() {
	const bool focus = searchFocus_;
	search_cls = focus ? "on" : "";
	search_icon = "/";
	if (focus) {
		search_before = search_.before();
		search_under = search_.under();
		search_after = search_.after();
		search_cursor_cls = "cursor";
	} else {
		search_before = search_.text();
		search_under.clear();
		search_after.clear();
		search_cursor_cls.clear();
	}
	search_placeholder = search_.empty() ? "search · sink:render  -noise  \"exact phrase\"  sev:err" : "";

	const std::size_t n = index_->size();
	const std::size_t total = store_.size();
	const std::string spin = kSpinner[static_cast<std::size_t>(spinner_) % kSpinner.size()];
	search_info_cls.clear();
	if (index_->rebuilding()) {
		search_info = spin + " filtering";
	} else if (!search_.empty()) {
		search_info = std::format("{} match{}", n, n == 1 ? "" : "es");
		if (n == 0) {
			search_info_cls = "bad";
		}
	} else if (n != total) {
		search_info = std::format("{} of {}", n, total);
	} else {
		search_info = std::format("{} log{}", total, total == 1 ? "" : "s");
	}
}

bool App::searchChanged() {
	if (search_.text() == lastQueryText_) {
		return false;
	}
	lastQueryText_ = search_.text();
	index_->setQuery(Query::parse(lastQueryText_));
	return true;
}

void App::applyFilter(FilterState next, const char* undoLabel) {
	if (next == index_->filter()) {
		return;
	}
	undoStack_.push_back(index_->filter());
	if (undoStack_.size() > 30) {
		undoStack_.erase(undoStack_.begin());
	}
	index_->setFilter(std::move(next));
	if (undoLabel != nullptr) {
		toast(std::format("{} · u to undo", undoLabel), "warn");
	}
}

void App::toggleGroup(int group) {
	FilterState next = index_->filter();
	const std::uint8_t bits = groupBits(group);
	if ((next.sevMask & bits) == bits) {
		next.sevMask &= static_cast<std::uint8_t>(~bits);
	} else {
		next.sevMask |= bits;
	}
	applyFilter(std::move(next));
}

void App::undoLast() {
	if (clearedRecently_) {
		store_.undoClear();
		clearedRecently_ = false;
		index_->reset(store_.data());
		firstTs_ = store_.size() ? store_[0].ts : 0;
		follow_ = true;
		toast("Restored cleared logs", "good");
		return;
	}
	if (undoStack_.empty()) {
		toast("Nothing to undo", "warn");
		return;
	}
	index_->setFilter(std::move(undoStack_.back()));
	undoStack_.pop_back();
	toast("Undid last filter change", "good");
}

void App::clearAll() {
	const std::size_t n = store_.size();
	if (n == 0) {
		return;
	}
	store_.clear();
	index_->reset(store_.data());
	clearedRecently_ = true;
	clearedAt_ = Clock::now();
	sel_ = 0;
	scrollTop_ = 0;
	follow_ = true;
	firstTs_ = 0;
	haveSelRecord_ = false;
	toast(std::format("Cleared {} logs · u to undo", n), "warn");
}

void App::RowClick(std::string index) {
	consumed_ = true;
	if (overlay_ != Overlay::None) {
		return;
	}
	const std::size_t pos = scrollTop_ + static_cast<std::size_t>(std::atoi(index.c_str()));
	if (pos >= index_->size()) {
		return;
	}
	const auto now = Clock::now();
	const bool second = pos == lastClickPos_ && now - lastClick_ < std::chrono::milliseconds(450);
	lastClick_ = now;
	lastClickPos_ = pos;
	searchFocus_ = false;
	setSel(pos);
	if (second) {
		openDetail(pos);
	}
}

void App::RowContext(std::string index) {
	consumed_ = true;
	if (overlay_ != Overlay::None) {
		return;
	}
	const std::size_t pos = scrollTop_ + static_cast<std::size_t>(std::atoi(index.c_str()));
	if (pos >= index_->size()) {
		return;
	}
	setSel(pos);
	contextRequested_ = true;
	contextPosition_ = pos;
}

void App::BarJump(std::string index) {
	consumed_ = true;
	const std::size_t n = index_->size();
	const auto h = static_cast<double>(viewRows());
	if (n == 0 || overlay_ != Overlay::None) {
		return;
	}
	const double frac = (std::atof(index.c_str()) + 0.5) / h;
	follow_ = false;
	setSel(std::min(n - 1, static_cast<std::size_t>(frac * static_cast<double>(n))));
	ensureVisible(true);
}

void App::FocusSearch() {
	consumed_ = true;
	if (overlay_ == Overlay::None) {
		searchFocus_ = true;
	}
}

void App::ToggleGroup(std::string group) {
	consumed_ = true;
	toggleGroup(std::atoi(group.c_str()));
}

void App::runChord(char first, char second, int /*count*/) {
	const LogRecord* rec = selected();
	if (first == 'g' && second == 'g') {
		follow_ = false;
		setSel(0);
		ensureVisible(true);
	} else if (first == 'z' && (second == 'z' || second == 't' || second == 'b')) {
		const std::size_t h = viewRows();
		const std::size_t n = index_->size();
		if (second == 'z') {
			ensureVisible(true);
		} else if (second == 't') {
			scrollTop_ = std::min(sel_, n > h ? n - h : 0);
		} else {
			scrollTop_ = sel_ + 1 > h ? sel_ + 1 - h : 0;
		}
	} else if (first == 'h' && rec != nullptr) {
		const int which = second == 'f' ? 0 : second == 'h' ? 1 : second == 's' ? 2 : -1;
		if (which >= 0) {
			ctxPosition_ = (*index_)[sel_];
			runContextSub(which);
		}
	} else if (first == 'y' && rec != nullptr) {
		if (second == 'y') {
			copyText(recordText(*rec, true), "log entry");
		} else if (second == 'm') {
			copyText(rec->message, "message");
		} else if (second == 'b') {
			if (rec->backtrace == 0) {
				toast("This log has no backtrace", "warn");
			} else {
				copyText(std::string(store_.data()->backtraces().get(rec->backtrace)), "backtrace");
			}
		}
	}
}

bool App::handleTableKey(const Key& key) {
	using K = rtxui::Event::Keyboard;

	if (chord_ != 0) {
		const char first = chord_;
		chord_ = 0;
		if (key.ch > 0 && key.ch < 128 && !key.ctrl) {
			runChord(first, static_cast<char>(key.ch), 1);
		}
		return true;
	}

	const std::size_t h = viewRows();
	if (key.is(K::Escape)) {
		if (!search_.empty()) {
			search_.clear();
			searchChanged();
		}
		return true;
	}
	if (key.is('j') || key.is(K::ArrowDown)) {
		moveSel(1);
	} else if (key.is('k') || key.is(K::ArrowUp)) {
		moveSel(-1);
	} else if (key.isCtrl('d')) {
		moveSel(static_cast<long>(h / 2));
	} else if (key.isCtrl('u')) {
		moveSel(-static_cast<long>(h / 2));
	} else if (key.isCtrl('f') || key.is(K::PageDown)) {
		moveSel(static_cast<long>(h));
	} else if (key.isCtrl('b') || key.is(K::PageUp)) {
		moveSel(-static_cast<long>(h));
	} else if (key.is('G') || key.is(K::End)) {
		goBottom();
	} else if (key.is(K::Home)) {
		follow_ = false;
		setSel(0);
	} else if (key.is('g') || key.is('z') || key.is('y') || key.is('h')) {
		chord_ = static_cast<char>(key.ch);
		chordAt_ = Clock::now();
	} else if (key.is('H')) {
		setSel(scrollTop_);
	} else if (key.is('M')) {
		setSel(scrollTop_ + std::min(h, index_->size() - scrollTop_) / 2);
	} else if (key.is('L')) {
		setSel(scrollTop_ + std::min(h, index_->size() - scrollTop_) - 1);
	} else if (key.is('/')) {
		searchFocus_ = true;
	} else if (key.is(K::Return) || key.is('o') || key.is('l') || key.is(K::ArrowRight)) {
		if (selected() != nullptr) {
			openDetail(sel_);
		}
	} else if (key.is('m') || key.is(' ')) {
		if (selected() != nullptr) {
			openContext(sel_, 12, 1 + static_cast<int>(sel_ - scrollTop_));
		}
	} else if (key.is('s')) {
		openOverlay(Overlay::Sinks);
	} else if (key.is('f')) {
		openOverlay(Overlay::Severities);
	} else if (key.is('?')) {
		openOverlay(Overlay::Help);
	} else if (key.is('F')) {
		ToggleFollow();
	} else if (key.is('t')) {
		relativeTime_ = !relativeTime_;
	} else if (key.is('X')) {
		clearAll();
	} else if (key.is('u')) {
		undoLast();
	} else if (key.is('e')) {
		jumpToError(1);
	} else if (key.is('E')) {
		jumpToError(-1);
	} else if (key.is('1')) {
		toggleGroup(0);
	} else if (key.is('2')) {
		toggleGroup(1);
	} else if (key.is('3')) {
		toggleGroup(2);
	} else if (key.is('4')) {
		toggleGroup(3);
	} else if (key.is('q')) {
		leaveToMenu();
	} else {
		return false;
	}
	return true;
}

}  // namespace kenzo::ui
