#include "core/fold.hpp"

#include <algorithm>

namespace kenzo {

bool FoldMap::begin(std::size_t n, std::uint64_t epoch, const void* dataId) {
	if (dataId != dataId_) {
		expanded_.clear();
	}
	if (dataId == dataId_ && epoch == epoch_ && n >= scanned_) {
		return false;
	}
	dataId_ = dataId;
	epoch_ = epoch;
	groups_.clear();
	scanned_ = hidden_ = 0;
	runStart_ = runLen_ = 0;
	runKey_ = 0;
	runRec_ = 0;
	return true;
}

void FoldMap::push(const Entry& entry) {
	const std::size_t pos = scanned_++;
	if (entry.key != 0 && runLen_ > 0 && entry.key == runKey_) {
		++runLen_;
		if (runLen_ == 2) {
			Group g;
			g.start = static_cast<std::uint32_t>(runStart_);
			g.len = 2;
			g.rec = runRec_;
			g.hiddenBefore = hidden_;
			g.folded = !expanded_.contains(runRec_);
			hidden_ += hiddenBy(g);
			groups_.push_back(g);
		} else {
			Group& g = groups_.back();
			++g.len;
			if (g.folded) {
				++hidden_;
			}
		}
	} else {
		runStart_ = pos;
		runLen_ = 1;
		runKey_ = entry.key;
		runRec_ = entry.rec;
	}
}

std::size_t FoldMap::groupAtOrBefore(std::size_t pos) const {
	const auto it = std::upper_bound(groups_.begin(), groups_.end(), pos, [](std::size_t p, const Group& g) { return p < g.start; });
	return it == groups_.begin() ? groups_.size() : static_cast<std::size_t>(it - groups_.begin()) - 1;
}

FoldMap::Row FoldMap::at(std::size_t row) const {
	const auto it = std::upper_bound(groups_.begin(), groups_.end(), row, [&](std::size_t r, const Group& g) { return r < displayStart(g); });
	if (it == groups_.begin()) {
		return Row {.pos = row};
	}
	const Group& g = *(it - 1);
	const std::size_t offset = row - displayStart(g);
	const std::size_t span = g.folded ? 1 : g.len;
	if (offset < span) {
		return Row {.pos = g.start + offset, .len = g.len, .offset = offset, .grouped = true, .folded = g.folded};
	}
	return Row {.pos = static_cast<std::size_t>(g.start) + g.len + (offset - span)};
}

std::size_t FoldMap::rowOf(std::size_t pos) const {
	const std::size_t i = groupAtOrBefore(pos);
	if (i == groups_.size()) {
		return pos;
	}
	const Group& g = groups_[i];
	if (pos < static_cast<std::size_t>(g.start) + g.len) {
		return displayStart(g) + (g.folded ? 0 : pos - g.start);
	}
	return pos - (g.hiddenBefore + hiddenBy(g));
}

void FoldMap::setFolded(std::size_t index, bool folded) {
	Group& g = groups_[index];
	if (g.folded == folded) {
		return;
	}
	hidden_ -= hiddenBy(g);
	g.folded = folded;
	hidden_ += hiddenBy(g);
	if (folded) {
		expanded_.erase(g.rec);
	} else {
		expanded_.insert(g.rec);
	}
	std::size_t before = g.hiddenBefore + hiddenBy(g);
	for (std::size_t i = index + 1; i < groups_.size(); ++i) {
		groups_[i].hiddenBefore = before;
		before += hiddenBy(groups_[i]);
	}
}

bool FoldMap::groupOf(std::size_t pos, std::size_t& start, std::size_t& len) const {
	const std::size_t i = groupAtOrBefore(pos);
	if (i == groups_.size() || pos >= static_cast<std::size_t>(groups_[i].start) + groups_[i].len) {
		return false;
	}
	start = groups_[i].start;
	len = groups_[i].len;
	return true;
}

bool FoldMap::toggle(std::size_t pos) {
	const std::size_t i = groupAtOrBefore(pos);
	if (i == groups_.size() || pos >= static_cast<std::size_t>(groups_[i].start) + groups_[i].len) {
		return false;
	}
	setFolded(i, !groups_[i].folded);
	return true;
}

void FoldMap::expand(std::size_t pos) {
	const std::size_t i = groupAtOrBefore(pos);
	if (i != groups_.size() && pos < static_cast<std::size_t>(groups_[i].start) + groups_[i].len) {
		setFolded(i, false);
	}
}

}
