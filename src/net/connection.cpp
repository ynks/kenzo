#include "net/connection.hpp"

#include <algorithm>
#include <asio.hpp>
#include <cctype>
#include <atomic>
#include <chrono>
#include <thread>

#include "core/wire.hpp"

namespace kenzo {

bool parseTarget(const std::string& text, Target& out) {
	std::string s = text;
	const auto trim = [](std::string& v) {
		const auto b = v.find_first_not_of(" \t");
		const auto e = v.find_last_not_of(" \t");
		v = b == std::string::npos ? std::string() : v.substr(b, e - b + 1);
	};
	trim(s);
	if (s.empty()) {
		return false;
	}

	std::string host = s;
	std::uint16_t port = kDefaultPort;
	if (s.front() == '[') {
		const auto close = s.find(']');
		if (close == std::string::npos) {
			return false;
		}
		host = s.substr(1, close - 1);
		if (close + 1 < s.size() && s[close + 1] == ':') {
			port = static_cast<std::uint16_t>(std::strtoul(s.c_str() + close + 2, nullptr, 10));
		}
	} else if (const auto colon = s.find(':'); colon != std::string::npos && s.find(':', colon + 1) == std::string::npos) {
		host = s.substr(0, colon);
		port = static_cast<std::uint16_t>(std::strtoul(s.c_str() + colon + 1, nullptr, 10));
	}
	if (host.empty() || port == 0) {
		return false;
	}
	if (host.find(':') != std::string::npos) {
		asio::error_code ec;
		asio::ip::make_address(host, ec);
		if (ec) {
			return false;
		}
	} else if (!std::ranges::all_of(host, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_'; })) {
		return false;
	}
	out.host = host;
	out.port = port;
	out.lan = true;
	return true;
}

struct Connection::Impl : std::enable_shared_from_this<Impl> {
	Impl(Target t, Post p, Handlers h) : target(std::move(t)), post(std::move(p)), handlers(std::move(h)), socket(ctx), timer(ctx) {}

	Target target;
	Post post;
	Handlers handlers;
	std::atomic<bool> stopped = false;

	asio::io_context ctx;
	asio::ip::tcp::socket socket;
	asio::steady_timer timer;
	std::jthread thread;

	bool everConnected = false;
	std::chrono::milliseconds backoff {250};
	std::uint32_t frameLength = 0;
	std::vector<std::byte> frame;
	std::byte header[4] {};

	void run() {
		attempt();
		ctx.run();
	}

	void emit(State s) {
		post([self = shared_from_this(), s] {
			if (!self->stopped) {
				self->handlers.state(s);
			}
		});
	}

	void attempt() {
		if (stopped) {
			return;
		}
		socket = asio::ip::tcp::socket(ctx);
		auto resolver = std::make_shared<asio::ip::tcp::resolver>(ctx);
		resolver->async_resolve(target.host, std::to_string(target.port),
		                        [self = shared_from_this(), resolver](const asio::error_code& ec, asio::ip::tcp::resolver::results_type results) {
			                        if (ec || self->stopped) {
				                        self->retry();
				                        return;
			                        }
			                        self->connectTo(std::move(results));
		                        });
		armDeadline(std::chrono::seconds(3));  // dns too
	}

	void armDeadline(std::chrono::milliseconds limit) {
		timer.expires_after(limit);
		timer.async_wait([self = shared_from_this()](const asio::error_code& ec) {
			if (!ec) {
				self->socket.close();
			}
		});
	}

	void connectTo(asio::ip::tcp::resolver::results_type results) {
		armDeadline(std::chrono::seconds(1));
		asio::async_connect(socket, results, [self = shared_from_this()](const asio::error_code& ec, const asio::ip::tcp::endpoint&) {
			self->timer.cancel();
			if (ec || self->stopped) {
				self->retry();
				return;
			}
			asio::error_code ignored;
			self->socket.set_option(asio::ip::tcp::no_delay(true), ignored);
			self->everConnected = true;
			self->backoff = std::chrono::milliseconds(250);
			self->emit(State::Connected);
			self->readHeader();
		});
	}

	void retry() {
		if (stopped) {
			return;
		}
		asio::error_code ignored;
		socket.close(ignored);
		emit(everConnected ? State::Disconnected : State::Searching);
		timer.expires_after(backoff);
		backoff = std::min(backoff * 2, std::chrono::milliseconds(1000));
		timer.async_wait([self = shared_from_this()](const asio::error_code& ec) {
			if (!ec) {
				self->attempt();
			}
		});
	}

	void readHeader() {
		asio::async_read(socket, asio::buffer(header), [self = shared_from_this()](const asio::error_code& ec, std::size_t) {
			if (ec || self->stopped) {
				self->retry();
				return;
			}
			self->frameLength = (std::uint32_t(self->header[0]) << 24) | (std::uint32_t(self->header[1]) << 16) |
			                    (std::uint32_t(self->header[2]) << 8) | std::uint32_t(self->header[3]);
			if (self->frameLength == 0) {
				self->readHeader();
				return;
			}
			if (self->frameLength > wire::kMaxFrame) {
				self->retry();
				return;
			}
			self->frame.resize(self->frameLength);
			self->readBody();
		});
	}

	void readBody() {
		asio::async_read(socket, asio::buffer(frame), [self = shared_from_this()](const asio::error_code& ec, std::size_t) {
			if (ec || self->stopped) {
				self->retry();
				return;
			}
			if (auto logs = wire::decodeBatch(self->frame)) {
				self->post([self, logs = std::move(*logs)]() mutable {
					if (!self->stopped) {
						self->handlers.batch(std::move(logs));
					}
				});
			}
			self->readHeader();
		});
	}
};

Connection::Connection(Post post, Handlers handlers) : post_(std::move(post)), handlers_(std::move(handlers)) {}

Connection::~Connection() {
	stop();
}

void Connection::connect(Target target) {
	stop();
	impl_ = std::make_shared<Impl>(std::move(target), post_, handlers_);
	impl_->thread = std::jthread([impl = impl_] { impl->run(); });
	post_([impl = impl_, this] {
		if (!impl->stopped) {
			handlers_.state(State::Searching);
		}
	});
}

void Connection::stop() {
	if (!impl_) {
		return;
	}
	auto impl = std::move(impl_);
	impl->stopped = true;
	asio::post(impl->ctx, [impl] {
		asio::error_code ignored;
		impl->socket.close(ignored);
		impl->timer.cancel();
	});
	impl->ctx.stop();
	impl->thread.detach();  // finishes on its own
}

}  // namespace kenzo
