#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kenzo {

inline constexpr std::uint16_t kDiscoveryPort = 12802;

struct Peer {
	std::string name;
	std::string address;
	std::uint16_t port = 0;
};

bool parseReply(const std::string& payload, std::string& name, std::uint16_t& port);

class Discovery {
public:
	Discovery();
	~Discovery();
	Discovery(const Discovery&) = delete;
	Discovery& operator=(const Discovery&) = delete;
	[[nodiscard]] std::vector<Peer> peers() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

}
