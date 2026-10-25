#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/gunzip.hpp"

namespace kenzo::net {

struct HttpResult {
	long status = 0;
	std::string body;
	std::string error;

	[[nodiscard]] bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

struct HttpOptions {
	std::vector<std::string> headers;
};

using Sink = ByteSink;
using Progress = std::function<void(std::uint64_t done, std::uint64_t total)>;

HttpResult httpGet(const std::string& url, const HttpOptions& options = {});
HttpResult httpPostJson(const std::string& url, const std::string& json, const HttpOptions& options = {});

HttpResult httpDownload(const std::string& url, const HttpOptions& options, const Sink& sink, const Progress& progress = {},
                        const std::atomic_bool* cancel = nullptr);

}  // namespace kenzo::net
