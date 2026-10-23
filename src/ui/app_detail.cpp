#include <algorithm>
#include <format>

#include "core/format.hpp"
#include "ui/app.hpp"

namespace kenzo::ui {
namespace {

constexpr std::size_t kMaxLines = 6000;
constexpr const char* kBlank = "\xC2\xA0";  // keeps the row height

std::vector<std::size_t> charStarts(const std::string& text) {
	std::vector<std::size_t> starts;
	starts.reserve(text.size() + 1);
	for (std::size_t i = 0; i < text.size(); ++i) {
		if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) {
			starts.push_back(i);
		}
	}
	starts.push_back(text.size());
	return starts;
}

void wrapInto(std::vector<TextLine>& out, std::string_view text, int width, const std::string& cls) {
	width = std::max(8, width);
	std::size_t start = 0;
	while (start <= text.size() && out.size() < kMaxLines) {
		std::size_t end = text.find('\n', start);
		if (end == std::string_view::npos) end = text.size();
		std::string line(text.substr(start, end - start));
		start = end + 1;

		while (!line.empty() && line.back() == '\r') line.pop_back();
		for (std::size_t p = line.find('\t'); p != std::string::npos; p = line.find('\t', p)) line.replace(p, 1, "    ");
		for (char& c : line) {
			if (static_cast<unsigned char>(c) < 0x20) c = ' ';
		}

		if (line.empty()) {
			out.push_back({kBlank, cls});
			continue;
		}
		while (!line.empty() && out.size() < kMaxLines) {
			const auto starts = charStarts(line);
			const std::size_t chars = starts.size() - 1;
			if (chars <= static_cast<std::size_t>(width)) {
				out.push_back({line, cls});
				break;
			}
			std::size_t cut = static_cast<std::size_t>(width);
			for (std::size_t i = static_cast<std::size_t>(width); i > static_cast<std::size_t>(width) / 2; --i) {
				if (line[starts[i]] == ' ') {
					cut = i;
					break;
				}
			}
			std::string head = line.substr(0, starts[cut]);
			while (!head.empty() && head.back() == ' ') head.pop_back();
			out.push_back({head.empty() ? std::string(kBlank) : head, cls});
			std::size_t next = starts[cut];
			while (next < line.size() && line[next] == ' ') ++next;
			line.erase(0, next);
		}
	}
}

std::vector<BarCell> makeBar(std::size_t total, std::size_t visible, std::size_t top) {
	std::vector<BarCell> cells(visible);
	if (total > visible && visible > 0) {
		const double scale = static_cast<double>(visible) / static_cast<double>(total);
		const auto thumbTop = static_cast<std::size_t>(static_cast<double>(top) * scale);
		const auto thumbLen = std::max<std::size_t>(1, static_cast<std::size_t>(static_cast<double>(visible) * scale + 0.5));
		for (std::size_t i = thumbTop; i < std::min(visible, thumbTop + thumbLen); ++i) {
			cells[i].cls = "th";
		}
	}
	return cells;
}

}  // namespace

void App::openDetail(std::size_t position) {
	if (position >= index_->size()) return;
	detailPos_ = position;
	setSel(position);
	detailPane_ = 0;
	detailDirty_ = true;
	dvMsgTop_ = dvBtTop_ = 0;
	overlay_ = Overlay::Detail;
}

void App::buildDetail() {
	const std::size_t n = index_->size();
	if (n == 0) {
		closeOverlay();
		return;
	}
	detailPos_ = std::min(detailPos_, n - 1);
	const LogRecord& rec = store_[(*index_)[detailPos_]];
	const kenzo::Data& data = *store_.data();

	dv_sev_cls = std::format("sev{}", rec.sev);
	dv_bar_cls = dv_sev_cls;
	dv_sev = std::string(severityName(rec.sev));
	const auto sink = data.sinks().get(rec.sink);
	dv_sink = sink.empty() ? "(no sink)" : std::string(sink);
	dv_pos = std::format("{} / {}", detailPos_ + 1, n);
	dv_time = fmt::fullTime(rec.ts);
	const auto file = data.files().get(rec.file);
	dv_file = file.empty() ? "—" : std::format("{}:{}", file, rec.line);
	dv_has_details = !rec.details.empty();
	dv_has_bt = rec.backtrace != 0;
	dv_bt_btn_cls = dv_has_bt ? "" : "dis";
	dv_msg_focus = detailPane_ == 0 ? "focus" : "";
	dv_bt_focus = detailPane_ == 1 ? "focus" : "";
	if (detailPane_ == 1 && !dv_has_bt) detailPane_ = 0;

	dv_w = std::clamp(cols_ * 94 / 100, std::min(cols_, 40), cols_);
	dv_h = std::clamp(rowsTotal_ * 90 / 100, std::min(rowsTotal_, 12), rowsTotal_);
	const int inner = dv_w - 4;
	const int panesH = std::max(4, dv_h - 7);
	const bool side = dv_has_bt && cols_ >= 110;
	dv_layout = dv_has_bt && !side ? "stack" : "";
	if (!dv_has_bt) {
		dv_lw = inner;
		dv_lh = panesH;
	} else if (side) {
		dv_lw = inner / 2;
		dv_rw = inner - dv_lw;
		dv_lh = dv_rh = panesH;
	} else {
		dv_lw = dv_rw = inner;
		dv_lh = panesH / 2;
		dv_rh = panesH - dv_lh;
	}
	const int msgWidth = dv_lw - 5;
	const int btWidth = dv_rw - 5;
	dvMsgVis_ = std::max(1, dv_lh - 2);
	dvBtVis_ = std::max(1, dv_rh - 3);

	if (detailDirty_ || msgWidth != dvMsgWidth_ || btWidth != dvBtWidth_) {
		detailDirty_ = false;
		dvMsgWidth_ = msgWidth;
		dvBtWidth_ = btWidth;
		dvMsgAll_.clear();
		wrapInto(dvMsgAll_, rec.message, msgWidth, "");
		if (dv_has_details) {
			std::string rule;
			for (int i = 0; i < msgWidth; ++i) rule += "─";
			dvMsgAll_.push_back({rule, "sep"});
			wrapInto(dvMsgAll_, rec.details, msgWidth, "det");
		}
		dvBtAll_.clear();
		if (dv_has_bt) {
			std::string trace(data.backtraces().get(rec.backtrace));
			while (!trace.empty() && (trace.back() == '\n' || trace.back() == '\r')) trace.pop_back();
			wrapInto(dvBtAll_, trace, btWidth, "");
			dv_bt_count = std::to_string(std::count(trace.begin(), trace.end(), '\n') + 1);
		} else {
			dv_bt_count = "0";
		}
	}

	const auto slice = [](const std::vector<TextLine>& all, std::size_t& top, int visible, std::vector<TextLine>& out) {
		const auto vis = static_cast<std::size_t>(visible);
		top = std::min(top, all.size() > vis ? all.size() - vis : 0);
		out.assign(all.begin() + static_cast<long>(top), all.begin() + static_cast<long>(std::min(all.size(), top + vis)));
	};
	slice(dvMsgAll_, dvMsgTop_, dvMsgVis_, dv_msg_lines);
	slice(dvBtAll_, dvBtTop_, dvBtVis_, dv_bt_lines);
	dv_msg_bar = makeBar(dvMsgAll_.size(), static_cast<std::size_t>(dvMsgVis_), dvMsgTop_);
	dv_bt_bar = makeBar(dvBtAll_.size(), static_cast<std::size_t>(dvBtVis_), dvBtTop_);
}

std::string App::recordText(const LogRecord& rec, bool includeBacktrace) const {
	const kenzo::Data& data = *store_.data();
	const auto file = data.files().get(rec.file);
	std::string out = std::format("[{}] {}  {}{}  {}\n{}", severityName(rec.sev), data.sinks().get(rec.sink), file,
	                              file.empty() ? "" : std::format(":{}", rec.line), fmt::fullTime(rec.ts), rec.message);
	if (!rec.details.empty()) {
		out += "\n\n" + rec.details;
	}
	if (includeBacktrace && rec.backtrace != 0) {
		out += "\n\nBacktrace:\n" + std::string(data.backtraces().get(rec.backtrace));
	}
	return out;
}

void App::scrollPane(int pane, long delta) {
	const auto& all = pane == 0 ? dvMsgAll_ : dvBtAll_;
	auto& top = pane == 0 ? dvMsgTop_ : dvBtTop_;
	const auto vis = static_cast<long>(pane == 0 ? dvMsgVis_ : dvBtVis_);
	const long maxTop = std::max<long>(0, static_cast<long>(all.size()) - vis);
	top = static_cast<std::size_t>(std::clamp<long>(static_cast<long>(top) + delta, 0, maxTop));
}

int App::paneAt(int x, int y) const {
	const int left = (cols_ - dv_w) / 2 + 2;
	const int top = (rowsTotal_ - dv_h) / 2 + 1 + 2 + 1;
	if (dv_layout == "stack") {
		if (x < left || x >= left + dv_lw) return -1;
		if (y >= top && y < top + dv_lh) return 0;
		if (dv_has_bt && y >= top + dv_lh && y < top + dv_lh + dv_rh) return 1;
		return -1;
	}
	if (y < top || y >= top + dv_lh) return -1;
	if (x >= left && x < left + dv_lw) return 0;
	if (dv_has_bt && x >= left + dv_lw && x < left + dv_lw + dv_rw) return 1;
	return -1;
}

bool App::handleDetailKey(const Key& key) {
	using K = rtxui::Event::Keyboard;
	const long page = std::max(2, (detailPane_ == 0 ? dvMsgVis_ : dvBtVis_) / 2);

	if (chord_ != 0) {
		const char first = chord_;
		chord_ = 0;
		if (key.ch > 0 && key.ch < 128 && !key.ctrl) {
			runChord(first, static_cast<char>(key.ch), 1);
		}
		return true;
	}
	if (key.is(K::Escape) || key.is('q')) {
		closeOverlay();
	} else if (key.is('j') || key.is(K::ArrowDown)) {
		scrollPane(detailPane_, 1);
	} else if (key.is('k') || key.is(K::ArrowUp)) {
		scrollPane(detailPane_, -1);
	} else if (key.isCtrl('d') || key.is(K::PageDown)) {
		scrollPane(detailPane_, page);
	} else if (key.isCtrl('u') || key.is(K::PageUp)) {
		scrollPane(detailPane_, -page);
	} else if (key.is('g') || key.is(K::Home)) {
		scrollPane(detailPane_, -1'000'000);
	} else if (key.is('G') || key.is(K::End)) {
		scrollPane(detailPane_, 1'000'000);
	} else if (key.is('h') || key.is(K::ArrowLeft)) {
		detailPane_ = 0;
	} else if (key.is('l') || key.is(K::ArrowRight)) {
		if (dv_has_bt) detailPane_ = 1;
	} else if (key.is(K::Tab)) {
		detailPane_ = (dv_has_bt && detailPane_ == 0) ? 1 : 0;
	} else if (key.is('[') || key.is('K')) {
		if (detailPos_ > 0) {
			openDetail(detailPos_ - 1);
		}
	} else if (key.is(']') || key.is('J')) {
		if (detailPos_ + 1 < index_->size()) {
			openDetail(detailPos_ + 1);
		}
	} else if (key.is('y')) {
		chord_ = 'y';
		chordAt_ = Clock::now();
	} else {
		return false;
	}
	return true;
}

void App::FocusPane(std::string index) {
	consumed_ = true;
	detailPane_ = std::atoi(index.c_str()) == 1 && dv_has_bt ? 1 : 0;
}

void App::PaneBarMsg(std::string index) {
	consumed_ = true;
	detailPane_ = 0;
	const double frac = (std::atof(index.c_str()) + 0.5) / static_cast<double>(dvMsgVis_);
	const auto center = static_cast<long>(frac * static_cast<double>(dvMsgAll_.size()));
	dvMsgTop_ = 0;
	scrollPane(0, center - dvMsgVis_ / 2);
}

void App::PaneBarBt(std::string index) {
	consumed_ = true;
	detailPane_ = 1;
	const double frac = (std::atof(index.c_str()) + 0.5) / static_cast<double>(dvBtVis_);
	const auto center = static_cast<long>(frac * static_cast<double>(dvBtAll_.size()));
	dvBtTop_ = 0;
	scrollPane(1, center - dvBtVis_ / 2);
}

void App::DetailCopy() {
	consumed_ = true;
	if (const LogRecord* rec = selected()) copyText(recordText(*rec, true), "log entry");
}

void App::DetailCopyMessage() {
	consumed_ = true;
	if (const LogRecord* rec = selected()) copyText(rec->message, "message");
}

void App::DetailCopyBacktrace() {
	consumed_ = true;
	const LogRecord* rec = selected();
	if (rec == nullptr || rec->backtrace == 0) {
		toast("This log has no backtrace", "warn");
		return;
	}
	copyText(std::string(store_.data()->backtraces().get(rec->backtrace)), "backtrace");
}

}  // namespace kenzo::ui
