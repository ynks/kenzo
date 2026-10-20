#include "core/store.hpp"

#include "core/utf8.hpp"

namespace kenzo {

bool Data::append(RawRecord&& raw) {
	const std::size_t index = size_.load(std::memory_order_relaxed);
	const std::size_t chunk = index >> kChunkBits;
	if (chunk >= kMaxChunks) {
		return false;
	}
	if (!chunks_[chunk]) {
		chunks_[chunk] = std::make_unique<LogRecord[]>(kChunkSize);
	}

	for (std::string* s : {&raw.sink, &raw.file, &raw.message, &raw.details, &raw.backtrace}) {
		repairUtf8(*s);
	}

	LogRecord& rec = chunks_[chunk][index & (kChunkSize - 1)];
	rec.ts = raw.ts;
	rec.line = raw.line;
	rec.sev = raw.sev < kSevCount ? raw.sev : static_cast<std::uint8_t>(kInfo);
	rec.sink = sinks_.intern(raw.sink);
	rec.file = files_.intern(raw.file);
	rec.backtrace = raw.backtrace.empty() ? 0 : backtraces_.intern(raw.backtrace);
	rec.message = std::move(raw.message);
	rec.details = std::move(raw.details);

	++sevCounts_[rec.sev];
	if (sinkCounts_.size() <= rec.sink) {
		sinkCounts_.resize(rec.sink + 1);
	}
	++sinkCounts_[rec.sink][rec.sev];

	size_.store(index + 1, std::memory_order_release);
	return true;
}

}  // namespace kenzo

namespace kenzo {

void Store::undoClear() {
	if (!undo_) {
		return;
	}
	auto restored = std::move(undo_);
	undo_.reset();
	for (std::size_t i = 0; i < data_->size(); ++i) {
		const LogRecord& r = (*data_)[i];
		RawRecord raw;
		raw.ts = r.ts;
		raw.line = r.line;
		raw.sev = r.sev;
		raw.sink = data_->sinks().get(r.sink);
		raw.file = data_->files().get(r.file);
		raw.message = r.message;
		raw.details = r.details;
		raw.backtrace = r.backtrace == 0 ? std::string() : std::string(data_->backtraces().get(r.backtrace));
		restored->append(std::move(raw));
	}
	data_ = std::move(restored);
}

}  // namespace kenzo
