#include "core/filter.hpp"

#include <algorithm>

namespace kenzo {

ViewIndex::ViewIndex(Post post) : post_(std::move(post)), state_(std::make_shared<State>()) {}

ViewIndex::~ViewIndex() {
	worker_.request_stop();
}

bool ViewIndex::pass(const Data& data, const FilterState& f, const Query& q, std::size_t i) {
	const LogRecord& rec = data[i];
	return f.allows(rec.sink, rec.sev) && (q.empty() || q.matches(data, rec));
}

void ViewIndex::reset(std::shared_ptr<Data> data) {
	worker_.request_stop();
	worker_ = {};  // joins
	++state_->generation;
	++state_->epoch;
	state_->data = std::move(data);
	state_->view.clear();
	state_->hot.clear();
	state_->scanned = 0;
	state_->rebuilding = false;
	rebuild();
}

void ViewIndex::setFilter(FilterState filter) {
	if (filter == state_->filter) {
		return;
	}
	state_->filter = std::move(filter);
	rebuild();
}

void ViewIndex::setQuery(Query query) {
	state_->query = std::make_shared<Query>(std::move(query));
	rebuild();
}

void ViewIndex::sync() {
	State& s = *state_;
	if (!s.data || s.rebuilding) {
		return;
	}
	const std::size_t end = s.data->size();
	for (; s.scanned < end; ++s.scanned) {
		if (pass(*s.data, s.filter, *s.query, s.scanned)) {
			const auto sev = (*s.data)[s.scanned].sev;
			if (sev >= kWarning) {
				s.hot.emplace_back(static_cast<std::uint32_t>(s.view.size()), sev);
			}
			s.view.push_back(static_cast<std::uint32_t>(s.scanned));
		}
	}
}

void ViewIndex::rebuild() {
	State& s = *state_;
	if (!s.data) {
		return;
	}
	worker_.request_stop();
	worker_ = {};
	const std::uint64_t generation = ++s.generation;
	const std::size_t end = s.data->size();

	if (end < 20'000) {
		s.view.clear();
		s.hot.clear();
		s.scanned = 0;
		s.rebuilding = false;
		++s.epoch;
		sync();
		return;
	}

	s.rebuilding = true;
	worker_ = std::jthread([this, state = state_, data = s.data, filter = s.filter, query = s.query, end,
	                        generation](std::stop_token stop) {
		std::vector<std::uint32_t> result;
		std::vector<std::pair<std::uint32_t, std::uint8_t>> hot;
		for (std::size_t i = 0; i < end; ++i) {
			if ((i & 4095) == 0 && stop.stop_requested()) {
				return;
			}
			if (pass(*data, filter, *query, i)) {
				if ((*data)[i].sev >= kWarning) {
					hot.emplace_back(static_cast<std::uint32_t>(result.size()), (*data)[i].sev);
				}
				result.push_back(static_cast<std::uint32_t>(i));
			}
		}
		post_([state, generation, end, result = std::move(result), hot = std::move(hot)]() mutable {
			if (state->generation != generation) {
				return;
			}
			state->view = std::move(result);
			state->hot = std::move(hot);
			state->scanned = end;
			state->rebuilding = false;
			++state->epoch;
		});
	});
}

std::size_t ViewIndex::lowerBound(std::uint32_t recordIndex) const {
	const auto& v = state_->view;
	return static_cast<std::size_t>(std::lower_bound(v.begin(), v.end(), recordIndex) - v.begin());
}

}  // namespace kenzo
