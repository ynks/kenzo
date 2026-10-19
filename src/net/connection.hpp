#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/record.hpp"

namespace kenzo {

inline constexpr std::uint16_t kDefaultPort = 12801;

struct Target {
	std::string host = "127.0.0.1";
	std::uint16_t port = kDefaultPort;
	bool lan = false;

	[[nodiscard]] std::string display() const { return lan ? host + (port == kDefaultPort ? "" : ":" + std::to_string(port)) : "localhost"; }
};

bool parseTarget(const std::string& text, Target& out);

class Connection {
public:
	enum class State { Searching, Connected, Disconnected };

	using Post = std::function<void(std::function<void()>)>;

	struct Handlers {
		std::function<void(State)> state;
		std::function<void(std::vector<RawRecord>&&)> batch;
	};

	Connection(Post post, Handlers handlers);
	~Connection();
	Connection(const Connection&) = delete;
	Connection& operator=(const Connection&) = delete;

	void connect(Target target);
	void stop();

private:
	struct Impl;
	Post post_;
	Handlers handlers_;
	std::shared_ptr<Impl> impl_;
};

}  // namespace kenzo
