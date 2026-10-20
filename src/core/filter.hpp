#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "core/query.hpp"

namespace kenzo {

struct FilterState {
	static constexpr std::uint8_t kAll = (1u << kSevCount) - 1;

	std::uint8_t sevMask = kAll;
	std::vector<std::uint8_t> sinkBits;

	[[nodiscard]] std::uint8_t bitsFor(StrId sink) const { return sink < sinkBits.size() ? sinkBits[sink] : kAll; }
	[[nodiscard]] bool allows(StrId sink, int sev) const { return ((sevMask & bitsFor(sink)) >> sev) & 1u; }
	void setSinkBits(StrId sink, std::uint8_t bits) {
		if (sinkBits.size() <= sink) {
			sinkBits.resize(sink + 1, kAll);
		}
		sinkBits[sink] = bits & kAll;
	}
	bool operator==(const FilterState&) const = default;
};

class ViewIndex {
public:
	using Post = std::function<void(std::function<void()>)>;

	explicit ViewIndex(Post post);
	~ViewIndex();
	ViewIndex(const ViewIndex&) = delete;
	ViewIndex& operator=(const ViewIndex&) = delete;

	void reset(std::shared_ptr<Data> data);

	void setFilter(FilterState filter);
	void setQuery(Query query);

	void sync();

	[[nodiscard]] const FilterState& filter() const { return state_->filter; }
	[[nodiscard]] const Query& query() const { return *state_->query; }
	[[nodiscard]] bool rebuilding() const { return state_->rebuilding; }
	[[nodiscard]] std::size_t size() const { return state_->view.size(); }
	[[nodiscard]] std::uint32_t operator[](std::size_t i) const { return state_->view[i]; }
	[[nodiscard]] std::uint64_t epoch() const { return state_->epoch; }
	[[nodiscard]] const std::vector<std::pair<std::uint32_t, std::uint8_t>>& hot() const { return state_->hot; }

	[[nodiscard]] std::size_t lowerBound(std::uint32_t recordIndex) const;

private:
	struct State {
		std::shared_ptr<Data> data;
		FilterState filter;
		std::shared_ptr<const Query> query = std::make_shared<Query>();
		std::vector<std::uint32_t> view;
		std::vector<std::pair<std::uint32_t, std::uint8_t>> hot;
		std::size_t scanned = 0;
		std::uint64_t epoch = 0;
		std::uint64_t generation = 0;
		bool rebuilding = false;
	};

	void rebuild();
	static bool pass(const Data& data, const FilterState& f, const Query& q, std::size_t i);

	Post post_;
	std::shared_ptr<State> state_;
	std::jthread worker_;
};

}  // namespace kenzo
