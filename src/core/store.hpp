#pragma once

#include "core/record.hpp"

namespace kenzo {

class Data {
public:
	static constexpr std::size_t kChunkBits = 14;
	static constexpr std::size_t kChunkSize = std::size_t{1} << kChunkBits;
	static constexpr std::size_t kMaxChunks = std::size_t{1} << 16;

	Data() : chunks_(std::make_unique<std::unique_ptr<LogRecord[]>[]>(kMaxChunks)) {}

	[[nodiscard]] std::size_t size() const { return size_.load(std::memory_order_acquire); }

	[[nodiscard]] const LogRecord& operator[](std::size_t i) const {
		return chunks_[i >> kChunkBits][i & (kChunkSize - 1)];
	}

	bool append(RawRecord&& raw);

	[[nodiscard]] const Interner& sinks() const { return sinks_; }
	[[nodiscard]] const Interner& files() const { return files_; }
	[[nodiscard]] const Interner& backtraces() const { return backtraces_; }

	[[nodiscard]] std::uint64_t severityCount(int sev) const { return sevCounts_[static_cast<std::size_t>(sev)]; }
	[[nodiscard]] const std::vector<std::array<std::uint32_t, kSevCount>>& sinkCounts() const { return sinkCounts_; }

private:
	std::unique_ptr<std::unique_ptr<LogRecord[]>[]> chunks_;
	std::atomic<std::size_t> size_ = 0;
	Interner sinks_, files_, backtraces_;
	std::array<std::uint64_t, kSevCount> sevCounts_ {};
	std::vector<std::array<std::uint32_t, kSevCount>> sinkCounts_;
};

class Store {
public:
	Store() : data_(std::make_shared<Data>()) {}

	[[nodiscard]] const std::shared_ptr<Data>& data() const { return data_; }
	[[nodiscard]] std::size_t size() const { return data_->size(); }
	[[nodiscard]] const LogRecord& operator[](std::size_t i) const { return (*data_)[i]; }

	bool append(RawRecord&& raw) { return data_->append(std::move(raw)); }

	void clear() {
		undo_ = std::move(data_);
		data_ = std::make_shared<Data>();
	}

	[[nodiscard]] bool canUndoClear() const { return undo_ != nullptr; }

	void undoClear();

	void dropUndo() { undo_.reset(); }

private:
	std::shared_ptr<Data> data_;
	std::shared_ptr<Data> undo_;
};

}  // namespace kenzo
