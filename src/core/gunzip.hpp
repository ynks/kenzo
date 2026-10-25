#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <span>

namespace kenzo {

using ByteSink = std::function<bool(std::span<const std::byte>)>;

class Gunzip {
public:
	explicit Gunzip(ByteSink out);
	~Gunzip();
	Gunzip(const Gunzip&) = delete;
	Gunzip& operator=(const Gunzip&) = delete;

	bool write(std::span<const std::byte> compressed);

	bool finish();

private:
	struct State;
	std::unique_ptr<State> state_;
	ByteSink out_;
};

}  // namespace kenzo
