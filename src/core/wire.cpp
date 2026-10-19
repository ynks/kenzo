#include "core/wire.hpp"

#include <cstdint>
#include <string_view>

namespace kenzo::wire {
namespace {

class Reader {
public:
	explicit Reader(std::span<const std::byte> bytes) : p_(bytes.data()), end_(bytes.data() + bytes.size()) {}

	[[nodiscard]] bool done() const { return p_ >= end_; }

	bool varint(std::uint64_t& out) {
		out = 0;
		for (int shift = 0; shift < 64; shift += 7) {
			if (p_ >= end_) {
				return false;
			}
			const auto byte = static_cast<std::uint8_t>(*p_++);
			out |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
			if ((byte & 0x80) == 0) {
				return true;
			}
		}
		return false;
	}

	bool bytes(std::span<const std::byte>& out) {
		std::uint64_t len = 0;
		if (!varint(len) || len > static_cast<std::uint64_t>(end_ - p_)) {
			return false;
		}
		out = {p_, static_cast<std::size_t>(len)};
		p_ += len;
		return true;
	}

	bool skip(std::uint32_t wireType) {
		std::uint64_t scratch = 0;
		switch (wireType) {
			case 0: return varint(scratch);
			case 1: return advance(8);
			case 2: {
				std::span<const std::byte> ignored;
				return bytes(ignored);
			}
			case 5: return advance(4);
			default: return false;
		}
	}

private:
	bool advance(std::size_t n) {
		if (static_cast<std::size_t>(end_ - p_) < n) {
			return false;
		}
		p_ += n;
		return true;
	}

	const std::byte* p_;
	const std::byte* end_;
};

std::string toString(std::span<const std::byte> bytes) {
	return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

bool decodeLog(std::span<const std::byte> bytes, RawRecord& out) {
	out.sev = kTrace;
	Reader r(bytes);
	while (!r.done()) {
		std::uint64_t tag = 0;
		if (!r.varint(tag)) {
			return false;
		}
		const auto field = static_cast<std::uint32_t>(tag >> 3);
		const auto type = static_cast<std::uint32_t>(tag & 7);

		std::uint64_t value = 0;
		std::span<const std::byte> blob;
		switch (field) {
			case 1:
			case 2:
			case 4:
				if (type != 0 || !r.varint(value)) {
					return false;
				}
				if (field == 1) {
					out.ts = value;
				} else if (field == 2) {
					out.sev = value < kSevCount ? static_cast<std::uint8_t>(value) : static_cast<std::uint8_t>(kInfo);
				} else {
					out.line = static_cast<std::uint32_t>(value);
				}
				break;
			case 3:
			case 5:
			case 6:
			case 7:
			case 8:
				if (type != 2 || !r.bytes(blob)) {
					return false;
				}
				(field == 3   ? out.file
				 : field == 5 ? out.sink
				 : field == 6 ? out.message
				 : field == 7 ? out.details
				              : out.backtrace) = toString(blob);
				break;
			default:
				if (!r.skip(type)) {
					return false;
				}
		}
	}
	return true;
}

}  // namespace

std::expected<std::vector<RawRecord>, std::string> decodeBatch(std::span<const std::byte> bytes) {
	std::vector<RawRecord> logs;
	Reader r(bytes);
	while (!r.done()) {
		std::uint64_t tag = 0;
		if (!r.varint(tag)) {
			return std::unexpected("truncated tag");
		}
		if ((tag >> 3) == 1 && (tag & 7) == 2) {
			std::span<const std::byte> blob;
			if (!r.bytes(blob)) {
				return std::unexpected("truncated log");
			}
			RawRecord rec;
			if (!decodeLog(blob, rec)) {
				return std::unexpected("malformed log");
			}
			logs.push_back(std::move(rec));
		} else if (!r.skip(static_cast<std::uint32_t>(tag & 7))) {
			return std::unexpected("malformed batch");
		}
	}
	return logs;
}

}  // namespace kenzo::wire
