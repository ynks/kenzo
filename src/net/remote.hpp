#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "net/http.hpp"
#include "net/ssh_auth.hpp"

namespace kenzo {

struct RemoteLog {
	std::string id, host, user, file;
	std::chrono::system_clock::time_point started;
	std::chrono::seconds duration {0};
	// uncompressed bytes
	std::uint64_t size = 0;
};

std::string defaultServerUrl();

class RemoteClient {
public:
	enum class State { Idle, SigningIn, NeedPassphrase, Loading, Ready, Error };

	struct Snapshot {
		State state = State::Idle;
		std::string error;
		std::string keyName;
		int triesLeft = 3;
		std::vector<RemoteLog> logs;
	};

	using Post = std::function<void(std::function<void()>)>;
	using DownloadDone = std::function<void(std::expected<std::filesystem::path, std::string>)>;

	RemoteClient(Post post, std::string serverUrl = defaultServerUrl(), std::filesystem::path sshDir = net::defaultSshDir(),
	             std::filesystem::path downloadDir = {});
	~RemoteClient();
	RemoteClient(const RemoteClient&) = delete;
	RemoteClient& operator=(const RemoteClient&) = delete;

	void open();
	void submitPassphrase(std::string passphrase);
	void cancelPassphrase();

	void download(const RemoteLog& log, net::Progress progress, DownloadDone done);
	void cancelDownload();

	[[nodiscard]] const Snapshot& snapshot() const;

struct Impl;

private:
	std::shared_ptr<Impl> impl_;
};

}  // namespace kenzo
