#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace kenzo {

class FoldMap {
public:
	struct Entry {
		std::uint64_t key = 0;
		std::uint32_t rec = 0;
	};

	struct Row {
		std::size_t pos = 0;
		std::size_t len = 1;
		std::size_t offset = 0;
		bool grouped = false;
		bool folded = false;
	};

	// at(pos) returns the Entry for a view position
	template <class At>
	void sync(std::size_t n, std::uint64_t epoch, const void* dataId, At&& at) {
		if (begin(n, epoch, dataId)) {
			for (std::size_t i = 0; i < n; ++i) {
				push(at(i));
			}
		} else {
			for (std::size_t i = scanned_; i < n; ++i) {
				push(at(i));
			}
		}
	}

	[[nodiscard]] std::size_t rows() const { return scanned_ - hidden_; }
	[[nodiscard]] Row at(std::size_t row) const;
	[[nodiscard]] std::size_t rowOf(std::size_t pos) const;

	[[nodiscard]] bool groupOf(std::size_t pos, std::size_t& start, std::size_t& len) const;
	bool toggle(std::size_t pos);
	void expand(std::size_t pos);
	void forget() { expanded_.clear(); }

private:
	struct Group {
		std::uint32_t start = 0, len = 0, rec = 0;
		std::size_t hiddenBefore = 0;
		bool folded = true;
	};

	bool begin(std::size_t n, std::uint64_t epoch, const void* dataId);
	void push(const Entry& entry);
	[[nodiscard]] std::size_t displayStart(const Group& g) const { return g.start - g.hiddenBefore; }
	[[nodiscard]] static std::size_t hiddenBy(const Group& g) { return g.folded ? g.len - 1 : 0; }
	[[nodiscard]] std::size_t groupAtOrBefore(std::size_t pos) const;
	void setFolded(std::size_t group, bool folded);

	std::vector<Group> groups_;
	std::unordered_set<std::uint32_t> expanded_;
	std::size_t scanned_ = 0, hidden_ = 0;
	std::size_t runStart_ = 0, runLen_ = 0;
	std::uint64_t runKey_ = 0, epoch_ = ~0ull;
	std::uint32_t runRec_ = 0;
	const void* dataId_ = nullptr;
};

}
