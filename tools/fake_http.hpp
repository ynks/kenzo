#pragma once

// tiny http server for the checks

#include <algorithm>
#include <array>
#include <asio.hpp>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace kenzo::fake {

struct Request {
	std::string method, target, headers, body;

	[[nodiscard]] std::string header(std::string name) const {
		const auto lower = [](std::string text) {
			std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		};
		const std::string haystack = lower(headers);
		const auto at = haystack.find("\r\n" + lower(name) + ":");
		if (at == std::string::npos) {
			return {};
		}
		const auto start = at + 2 + name.size() + 1;
		const auto end = headers.find("\r\n", start);
		std::string value = headers.substr(start, end - start);
		while (!value.empty() && value.front() == ' ') {
			value.erase(value.begin());
		}
		return value;
	}
};

struct Reply {
	int status = 200;
	std::string body;
	std::string contentType = "application/json";
};

class FakeHttp {
public:
	using Handler = std::function<Reply(const Request&)>;

	explicit FakeHttp(Handler handler)
	    : handler_(std::move(handler)), acceptor_(context_, asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0)) {
		worker_ = std::thread([this] { serve(); });
	}
	~FakeHttp() {
		stopping_ = true;
		asio::error_code ec;
		acceptor_.close(ec);
		worker_.join();
	}
	FakeHttp(const FakeHttp&) = delete;
	FakeHttp& operator=(const FakeHttp&) = delete;

	[[nodiscard]] std::uint16_t port() const { return acceptor_.local_endpoint().port(); }
	[[nodiscard]] std::string url() const { return "http://127.0.0.1:" + std::to_string(port()); }
	[[nodiscard]] std::vector<Request> requests() {
		std::scoped_lock lock(mutex_);
		return seen_;
	}
	[[nodiscard]] std::size_t count(const std::string& target_prefix) {
		std::scoped_lock lock(mutex_);
		return static_cast<std::size_t>(std::ranges::count_if(seen_, [&](const Request& r) { return r.target.starts_with(target_prefix); }));
	}

private:
	void serve() {
		while (!stopping_) {
			asio::ip::tcp::socket socket(context_);
			asio::error_code ec;
			acceptor_.accept(socket, ec);
			if (ec) {
				continue;
			}
			handle(socket);
		}
	}

	void handle(asio::ip::tcp::socket& socket) {
		asio::error_code ec;
		asio::streambuf buffer;
		asio::read_until(socket, buffer, "\r\n\r\n", ec);
		if (ec) {
			return;
		}
		const std::string all(asio::buffers_begin(buffer.data()), asio::buffers_end(buffer.data()));
		const std::size_t head_end = all.find("\r\n\r\n") + 4;
		Request request;
		request.headers = all.substr(0, head_end);
		const std::string line = request.headers.substr(0, request.headers.find("\r\n"));
		const auto first = line.find(' ');
		request.method = line.substr(0, first);
		request.target = line.substr(first + 1, line.rfind(' ') - first - 1);
		request.body = all.substr(head_end);

		std::size_t length = 0;
		if (const std::string value = request.header("Content-Length"); !value.empty()) {
			length = static_cast<std::size_t>(std::stoull(value));
		}
		std::array<char, 65536> chunk {};
		while (request.body.size() < length) {
			const std::size_t got = socket.read_some(asio::buffer(chunk), ec);
			if (ec) {
				break;
			}
			request.body.append(chunk.data(), got);
		}
		const Reply reply = handler_(request);
		{
			std::scoped_lock lock(mutex_);
			seen_.push_back(request);
		}
		const std::string head = "HTTP/1.1 " + std::to_string(reply.status) + " X\r\nContent-Type: " + reply.contentType +
		                         "\r\nContent-Length: " + std::to_string(reply.body.size()) + "\r\nConnection: close\r\n\r\n";
		asio::write(socket, asio::buffer(head), ec);
		if (!ec) {
			asio::write(socket, asio::buffer(reply.body), ec);
		}
		socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
	}

	Handler handler_;
	asio::io_context context_;
	asio::ip::tcp::acceptor acceptor_;
	std::thread worker_;
	std::atomic<bool> stopping_ = false;
	std::mutex mutex_;
	std::vector<Request> seen_;
};

}  // namespace kenzo::fake
