#include "net/http.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <memory>

namespace kenzo::net {
namespace {

constexpr long kConnectTimeoutSeconds = 10;
constexpr long kTotalTimeoutSeconds = 30;
constexpr long kStallSeconds = 60;
constexpr std::size_t kErrorBodyLimit = 64 * 1024;

struct Easy {
	Easy() {
		static const bool ready = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
		if (ready) {
			handle = curl_easy_init();
		}
	}
	~Easy() { curl_easy_cleanup(handle); }
	Easy(const Easy&) = delete;
	Easy& operator=(const Easy&) = delete;
	CURL* handle = nullptr;
};

struct Headers {
	~Headers() { curl_slist_free_all(list); }
	void add(const std::string& line) { list = curl_slist_append(list, line.c_str()); }
	curl_slist* list = nullptr;
};

std::size_t appendBody(char* data, std::size_t size, std::size_t count, void* user) {
	auto* body = static_cast<std::string*>(user);
	if (body->size() < kErrorBodyLimit) {
		body->append(data, size * count);
	}
	return size * count;
}

void setCommon(CURL* handle, const std::string& url, Headers& headers, const HttpOptions& options) {
	curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
	curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSeconds);
	curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L);
	for (const auto& line : options.headers) {
		headers.add(line);
	}
}

HttpResult finish(CURL* handle, CURLcode code, HttpResult result) {
	curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &result.status);
	if (code != CURLE_OK && result.error.empty()) {
		result.error = curl_easy_strerror(code);
	}
	return result;
}

HttpResult request(const std::string& url, const HttpOptions& options, const std::string* json) {
	Easy easy;
	HttpResult result;
	if (easy.handle == nullptr) {
		result.error = "curl is unavailable";
		return result;
	}
	Headers headers;
	setCommon(easy.handle, url, headers, options);
	curl_easy_setopt(easy.handle, CURLOPT_TIMEOUT, kTotalTimeoutSeconds);
	if (json != nullptr) {
		headers.add("Content-Type: application/json");
		curl_easy_setopt(easy.handle, CURLOPT_POST, 1L);
		curl_easy_setopt(easy.handle, CURLOPT_POSTFIELDS, json->c_str());
		curl_easy_setopt(easy.handle, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(json->size()));
	}
	curl_easy_setopt(easy.handle, CURLOPT_HTTPHEADER, headers.list);
	curl_easy_setopt(easy.handle, CURLOPT_WRITEFUNCTION, appendBody);
	curl_easy_setopt(easy.handle, CURLOPT_WRITEDATA, &result.body);
	const CURLcode code = curl_easy_perform(easy.handle);
	return finish(easy.handle, code, std::move(result));
}

struct DownloadState {
	CURL* handle = nullptr;
	const Sink* sink = nullptr;
	const Progress* progress = nullptr;
	const std::atomic_bool* cancel = nullptr;
	std::string errorBody;
	bool cancelled = false;
};

std::size_t onData(char* data, std::size_t size, std::size_t count, void* user) {
	auto* state = static_cast<DownloadState*>(user);
	const std::size_t bytes = size * count;
	long status = 0;
	curl_easy_getinfo(state->handle, CURLINFO_RESPONSE_CODE, &status);
	if (status < 200 || status >= 300) {
		appendBody(data, size, count, &state->errorBody);
		return bytes;
	}
	if (!(*state->sink)(std::as_bytes(std::span(data, bytes)))) {
		state->cancelled = true;
		return 0;
	}
	return bytes;
}

int onProgress(void* user, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
	auto* state = static_cast<DownloadState*>(user);
	if (state->cancel != nullptr && state->cancel->load()) {
		state->cancelled = true;
		return 1;
	}
	if (*state->progress && now >= 0) {
		(*state->progress)(static_cast<std::uint64_t>(now), total > 0 ? static_cast<std::uint64_t>(total) : 0);
	}
	return 0;
}

}  // namespace

HttpResult httpGet(const std::string& url, const HttpOptions& options) {
	return request(url, options, nullptr);
}

HttpResult httpPostJson(const std::string& url, const std::string& json, const HttpOptions& options) {
	return request(url, options, &json);
}

HttpResult httpDownload(const std::string& url, const HttpOptions& options, const Sink& sink, const Progress& progress,
                        const std::atomic_bool* cancel) {
	Easy easy;
	HttpResult result;
	if (easy.handle == nullptr) {
		result.error = "curl is unavailable";
		return result;
	}
	Headers headers;
	setCommon(easy.handle, url, headers, options);
	DownloadState state;
	state.handle = easy.handle;
	state.sink = &sink;
	state.progress = &progress;
	state.cancel = cancel;
	curl_easy_setopt(easy.handle, CURLOPT_HTTPHEADER, headers.list);
	curl_easy_setopt(easy.handle, CURLOPT_LOW_SPEED_LIMIT, 1024L);
	curl_easy_setopt(easy.handle, CURLOPT_LOW_SPEED_TIME, kStallSeconds);
	curl_easy_setopt(easy.handle, CURLOPT_WRITEFUNCTION, onData);
	curl_easy_setopt(easy.handle, CURLOPT_WRITEDATA, &state);
	curl_easy_setopt(easy.handle, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(easy.handle, CURLOPT_XFERINFOFUNCTION, onProgress);
	curl_easy_setopt(easy.handle, CURLOPT_XFERINFODATA, &state);
	const CURLcode code = curl_easy_perform(easy.handle);
	result.body = std::move(state.errorBody);
	if (state.cancelled) {
		result.error = "cancelled";
	}
	return finish(easy.handle, code, std::move(result));
}

}  // namespace kenzo::net
