#include "core/gunzip.hpp"

#include <zlib.h>

#include <array>

namespace kenzo {

struct Gunzip::State {
	z_stream stream {};
	bool ended = false;
	bool failed = false;
};

Gunzip::Gunzip(ByteSink out) : state_(std::make_unique<State>()), out_(std::move(out)) {
	// 16 on top of the window bits selects gzip
	if (inflateInit2(&state_->stream, 15 + 16) != Z_OK) {
		state_->failed = true;
	}
}

Gunzip::~Gunzip() {
	inflateEnd(&state_->stream);
}

bool Gunzip::write(std::span<const std::byte> compressed) {
	if (state_->failed) {
		return false;
	}
	if (compressed.empty()) {
		return true;
	}
	if (state_->ended) {
		state_->failed = true;
		return false;
	}
	auto& stream = state_->stream;
	stream.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(compressed.data()));
	stream.avail_in = static_cast<uInt>(compressed.size());
	std::array<std::byte, 65536> buffer {};
	while (stream.avail_in > 0) {
		stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
		stream.avail_out = static_cast<uInt>(buffer.size());
		const int status = inflate(&stream, Z_NO_FLUSH);
		const std::size_t produced = buffer.size() - stream.avail_out;
		if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR) {
			state_->failed = true;
			return false;
		}
		if (produced > 0 && !out_(std::span<const std::byte>(buffer.data(), produced))) {
			state_->failed = true;
			return false;
		}
		if (status == Z_STREAM_END) {
			state_->ended = true;
			if (stream.avail_in > 0) {
				state_->failed = true;
				return false;
			}
			break;
		}
		if (status == Z_BUF_ERROR && produced == 0) {
			break;
		}
	}
	return true;
}

bool Gunzip::finish() {
	return !state_->failed && state_->ended;
}

}  // namespace kenzo
