#include "net/discovery.hpp"

#include <algorithm>
#include <array>
#include <asio.hpp>
#include <string_view>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace kenzo {
namespace {

constexpr std::string_view kMagic = "toast-log/1 ";
constexpr std::string_view kQuery = "toast-log?";
constexpr auto kQueryEvery = std::chrono::seconds(2);
constexpr auto kExpiry = std::chrono::seconds(6);

}  // namespace

bool parseReply(const std::string& payload, std::string& name, std::uint16_t& port) {
	if (!payload.starts_with(kMagic)) {
		return false;
	}
	const std::string rest = payload.substr(kMagic.size());
	char* end = nullptr;
	const unsigned long value = std::strtoul(rest.c_str(), &end, 10);
	if (end == rest.c_str() || value == 0 || value > 65535 || *end != ' ') {
		return false;
	}
	std::string text(end + 1);
	while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\0')) {
		text.pop_back();
	}
	if (text.empty() || text.size() > 64) {
		return false;
	}
	name = std::move(text);
	port = static_cast<std::uint16_t>(value);
	return true;
}

struct Discovery::Impl {
	struct Entry {
		Peer peer;
		std::chrono::steady_clock::time_point seen;
	};

	Impl() : socket(ctx), timer(ctx) {}

	asio::io_context ctx;
	asio::ip::udp::socket socket;
	asio::steady_timer timer;
	asio::ip::udp::endpoint from;
	std::array<char, 256> buffer {};
	std::string self;
	mutable std::mutex mutex;
	std::vector<Entry> entries;
	std::jthread thread;

	void start() {
		asio::error_code ec;
		self = asio::ip::host_name(ec);
		socket.open(asio::ip::udp::v4(), ec);
		if (ec) {
			return;
		}
		socket.set_option(asio::socket_base::broadcast(true), ec);
		socket.bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), 0), ec);
		if (ec) {
			return;
		}
		receive();
		query();
		thread = std::jthread([this] { ctx.run(); });
	}

	/// asks every log server on the LAN to answer, the replies come back to this socket
	void query() {
		asio::error_code ec;
		socket.send_to(asio::buffer(kQuery), asio::ip::udp::endpoint(asio::ip::address_v4::broadcast(), kDiscoveryPort), 0, ec);
		timer.expires_after(kQueryEvery);
		timer.async_wait([this](const asio::error_code& wait) {
			if (!wait) {
				query();
			}
		});
	}

	void receive() {
		socket.async_receive_from(asio::buffer(buffer), from, [this](const asio::error_code& ec, std::size_t n) {
			if (ec == asio::error::operation_aborted) {
				return;
			}
			if (!ec) {
				handle(std::string(buffer.data(), n));
			}
			receive();
		});
	}

	void handle(const std::string& payload) {
		Peer peer;
		if (!parseReply(payload, peer.name, peer.port)) {
			return;
		}
		peer.address = from.address().to_string();
		const auto now = std::chrono::steady_clock::now();
		std::lock_guard lock(mutex);
		const auto at = std::ranges::find_if(entries, [&](const Entry& e) { return e.peer.address == peer.address && e.peer.port == peer.port; });
		if (at != entries.end()) {
			at->peer.name = peer.name;
			at->seen = now;
		} else {
			entries.push_back({std::move(peer), now});
		}
	}
};

Discovery::Discovery() : impl_(std::make_unique<Impl>()) {
	impl_->start();
}

Discovery::~Discovery() {
	impl_->ctx.stop();
	asio::error_code ec;
	impl_->socket.close(ec);
	if (impl_->thread.joinable()) {
		impl_->thread.join();
	}
}

std::vector<Peer> Discovery::peers() const {
	const auto now = std::chrono::steady_clock::now();
	std::lock_guard lock(impl_->mutex);
	std::erase_if(impl_->entries, [&](const Impl::Entry& e) { return now - e.seen > kExpiry; });
	std::vector<Peer> out;
	for (const auto& e : impl_->entries) {
		if (!e.peer.name.empty() && e.peer.name != impl_->self) {
			out.push_back(e.peer);
		}
	}
	return out;
}

}  // namespace kenzo
