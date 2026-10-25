#include "net/remote.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <thread>

#include "core/gunzip.hpp"
#include "core/paths.hpp"

namespace kenzo {
namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::system_clock;
using json = nlohmann::json;

// KENZO_REMOTE_URL overrides it
constexpr const char* kBuiltInServer = "http://kenzo.nullptr.es:8080";
// 50 pages of 200
constexpr int kMaxPages = 50;
constexpr int kPassphraseTries = 3;
constexpr auto kTokenMargin = std::chrono::seconds(30);

std::string trimSlashes(std::string url) {
	while (!url.empty() && url.back() == '/') {
		url.pop_back();
	}
	return url;
}

// pocketbase date format in utc
std::optional<Clock::time_point> parseTime(const std::string& text) {
	int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, millis = 0;
	char tail = 0;
	const int fields = std::sscanf(text.c_str(), "%4d-%2d-%2d %2d:%2d:%2d.%3d%c", &year, &month, &day, &hour, &minute, &second, &millis, &tail);
	if (fields < 6 || month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60) {
		return std::nullopt;
	}
	const std::chrono::year_month_day date {std::chrono::year(year), std::chrono::month(static_cast<unsigned>(month)),
	                                        std::chrono::day(static_cast<unsigned>(day))};
	if (!date.ok()) {
		return std::nullopt;
	}
	return std::chrono::time_point_cast<Clock::duration>(std::chrono::sys_days(date) + std::chrono::hours(hour) + std::chrono::minutes(minute) +
	                                                      std::chrono::seconds(second) + std::chrono::milliseconds(fields >= 7 ? millis : 0));
}

std::uint64_t asUnsigned(const json& value) {
	if (value.is_number_unsigned()) {
		return value.get<std::uint64_t>();
	}
	if (value.is_number() && value.get<double>() >= 0) {
		return static_cast<std::uint64_t>(value.get<double>());
	}
	return 0;
}

// strip control characters since host and user are untrusted
std::string plainText(const std::string& text) {
	std::string out;
	for (std::size_t i = 0; i < text.size(); ++i) {
		const auto c = static_cast<unsigned char>(text[i]);
		const bool c1 = c == 0xC2 && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) >= 0x80 && static_cast<unsigned char>(text[i + 1]) <= 0x9F;
		if (c < 0x20 || c == 0x7F || c1) {
			out.push_back('?');
			i += c1 ? 1 : 0;
		} else {
			out.push_back(static_cast<char>(c));
		}
	}
	return out;
}

std::optional<RemoteLog> parseRow(const json& row) {
	if (!row.is_object()) {
		return std::nullopt;
	}
	for (const char* key : {"id", "host", "user", "file", "started"}) {
		if (!row.contains(key) || !row[key].is_string()) {
			return std::nullopt;
		}
	}
	if (!row.contains("duration_s") || !row["duration_s"].is_number() || !row.contains("size") || !row["size"].is_number()) {
		return std::nullopt;
	}
	const auto started = parseTime(row["started"].get<std::string>());
	if (!started) {
		return std::nullopt;
	}
	RemoteLog log;
	log.id = row["id"].get<std::string>();
	log.host = plainText(row["host"].get<std::string>());
	log.user = plainText(row["user"].get<std::string>());
	log.file = row["file"].get<std::string>();
	log.started = *started;
	log.duration = std::chrono::seconds(static_cast<std::int64_t>(asUnsigned(row["duration_s"])));
	log.size = asUnsigned(row["size"]);
	return log;
}

std::string sanitize(const std::string& text) {
	std::string out;
	for (const char c : text) {
		const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
		out.push_back(plain ? c : '_');
	}
	return out.empty() ? "unknown" : out;
}

std::string localFileName(const RemoteLog& log) {
	const auto day = std::chrono::floor<std::chrono::days>(log.started);
	const std::chrono::year_month_day date {day};
	const std::chrono::hh_mm_ss time {std::chrono::floor<std::chrono::seconds>(log.started - day)};
	std::array<char, 32> stamp {};
	std::snprintf(stamp.data(), stamp.size(), "%04d%02u%02u-%02d%02d%02d", static_cast<int>(date.year()), static_cast<unsigned>(date.month()),
	              static_cast<unsigned>(date.day()), static_cast<int>(time.hours().count()), static_cast<int>(time.minutes().count()),
	              static_cast<int>(time.seconds().count()));
	return sanitize(log.host) + "-" + sanitize(log.user) + "-" + stamp.data() + ".csv";
}

std::optional<std::string> base64urlDecode(const std::string& text) {
	std::string out;
	std::uint32_t bits = 0;
	int count = 0;
	for (const char c : text) {
		int value = 0;
		if (c >= 'A' && c <= 'Z') value = c - 'A';
		else if (c >= 'a' && c <= 'z') value = c - 'a' + 26;
		else if (c >= '0' && c <= '9') value = c - '0' + 52;
		else if (c == '-' || c == '+') value = 62;
		else if (c == '_' || c == '/') value = 63;
		else if (c == '=') break;
		else return std::nullopt;
		bits = (bits << 6) | static_cast<std::uint32_t>(value);
		count += 6;
		if (count >= 8) {
			count -= 8;
			out.push_back(static_cast<char>((bits >> count) & 0xFF));
		}
	}
	return out;
}

// exp claim of the jwt
Clock::time_point tokenExpiry(const std::string& token) {
	const auto first = token.find('.');
	const auto second = first == std::string::npos ? std::string::npos : token.find('.', first + 1);
	if (second == std::string::npos) {
		return {};
	}
	const auto payload = base64urlDecode(token.substr(first + 1, second - first - 1));
	if (!payload) {
		return {};
	}
	const auto claims = json::parse(*payload, nullptr, false);
	if (!claims.is_object() || !claims.contains("exp") || !claims["exp"].is_number()) {
		return {};
	}
	return Clock::time_point(std::chrono::seconds(claims["exp"].get<std::int64_t>()));
}

std::string describe(const net::HttpResult& result, const char* whatFailed) {
	if (result.status == 0) {
		return "can't reach log server";
	}
	if (result.status == 404) {
		return "server does not support remote logs";
	}
	if (result.status == 401 || result.status == 403) {
		return "not authorised";
	}
	if (result.status >= 500) {
		return std::string("server cannot ") + whatFailed;
	}
	return "server error " + std::to_string(result.status);
}

std::optional<std::string> stringField(const std::string& body, const char* key) {
	const auto parsed = json::parse(body, nullptr, false);
	if (!parsed.is_object() || !parsed.contains(key) || !parsed[key].is_string()) {
		return std::nullopt;
	}
	return parsed[key].get<std::string>();
}

}  // namespace

std::string defaultServerUrl() {
	if (const char* env = std::getenv("KENZO_REMOTE_URL"); env != nullptr && *env != '\0') {
		return trimSlashes(env);
	}
	return kBuiltInServer;
}

struct RemoteClient::Impl : std::enable_shared_from_this<Impl> {
	Post post;
	std::string server;
	fs::path sshDir;
	fs::path downloadDir;

	// ui thread only
	Snapshot snap;
	std::jthread worker;

	// shared with the worker
	std::mutex mutex;
	std::string token;
	Clock::time_point tokenExpires {};
	std::optional<std::string> passphrase;
	bool passphraseFresh = false;
	int triesLeft = kPassphraseTries;
	std::size_t keyIndex = 0;
	std::atomic_bool cancel = false;

	~Impl() {
		cancel = true;
		if (!passphrase) {
			return;
		}
		std::ranges::fill(*passphrase, '\0');
	}

	[[nodiscard]] bool tokenValid() {
		std::scoped_lock lock(mutex);
		return !token.empty() && tokenExpires - kTokenMargin > Clock::now();
	}

	[[nodiscard]] std::string bearer() {
		std::scoped_lock lock(mutex);
		return token;
	}

	void onUi(std::function<void(Impl&)> change) {
		post([weak = weak_from_this(), change = std::move(change)] {
			if (const auto self = weak.lock()) {
				change(*self);
			}
		});
	}

	void setState(State state, std::string error = {}, std::string keyName = {}) {
		int tries = 0;
		{
			std::scoped_lock lock(mutex);
			tries = triesLeft;
		}
		onUi([=](Impl& self) {
			self.snap.state = state;
			self.snap.error = error;
			self.snap.keyName = keyName;
			self.snap.triesLeft = tries;
		});
	}

	void startWorker(std::function<void(std::shared_ptr<Impl>)> job) {
		cancel = true;
		if (worker.joinable()) {
			worker.join();
		}
		cancel = false;
		worker = std::jthread([self = shared_from_this(), job = std::move(job)] { job(self); });
	}
};

namespace {

struct AuthOutcome {
	enum class Kind { Token, NeedPassphrase, Error } kind = Kind::Error;
	std::string token;
	std::string message;
	std::string keyName;
};

AuthOutcome failure(std::string message) {
	AuthOutcome outcome;
	outcome.message = std::move(message);
	return outcome;
}

// signs the challenge with each key until one works
AuthOutcome authenticate(RemoteClient::Impl& impl) {
	const auto challengeResult = net::httpGet(impl.server + "/kenzo/challenge");
	if (!challengeResult.ok() || !challengeResult.error.empty()) {
		return failure(describe(challengeResult, "issue challenges"));
	}
	const auto challenge = stringField(challengeResult.body, "challenge");
	if (!challenge) {
		return failure("server sent no challenge");
	}
	const auto keys = net::findSshKeys(impl.sshDir);
	if (keys.empty()) {
		return failure("no ed25519 key in ~/.ssh");
	}

	std::size_t start = 0;
	{
		std::scoped_lock lock(impl.mutex);
		start = std::min(impl.keyIndex, keys.size() - 1);
	}
	for (std::size_t i = start; i < keys.size(); ++i) {
		if (impl.cancel) {
			return failure("cancelled");
		}
		std::optional<std::string> passphrase;
		{
			std::scoped_lock lock(impl.mutex);
			passphrase = impl.passphrase;
		}
		const auto signature = net::signChallenge(keys[i], *challenge, passphrase);
		switch (signature.status) {
			case net::SignStatus::Ok: {
				const json body = {{"challenge", *challenge}, {"signature", signature.signature}};
				const auto auth = net::httpPostJson(impl.server + "/kenzo/auth", body.dump());
				if (auth.status == 200 && auth.error.empty()) {
					if (const auto token = stringField(auth.body, "token")) {
						AuthOutcome outcome;
						outcome.kind = AuthOutcome::Kind::Token;
						outcome.token = *token;
						return outcome;
					}
					return failure("server sent no token");
				}
				if (auth.status != 401) {
					return failure(describe(auth, "verify keys"));
				}
				break;  // key not whitelisted so try the next
			}
			case net::SignStatus::NeedPassphrase:
			case net::SignStatus::WrongPassphrase: {
				std::scoped_lock lock(impl.mutex);
				impl.keyIndex = i;
				if (signature.status == net::SignStatus::WrongPassphrase) {
					if (impl.passphraseFresh) {
						impl.passphraseFresh = false;
						--impl.triesLeft;
						if (impl.triesLeft <= 0) {
							return failure("wrong passphrase");
						}
					}
					impl.passphrase.reset();
				}
				AuthOutcome outcome;
				outcome.kind = AuthOutcome::Kind::NeedPassphrase;
				outcome.keyName = keys[i].name;
				return outcome;
			}
			case net::SignStatus::NoSshKeygen: return failure("OpenSSH not found");
			case net::SignStatus::TooOld: return failure("OpenSSH too old (need 8.4 or newer)");
			case net::SignStatus::Failed: break;
		}
	}
	return failure("no whitelisted key (add ~/.ssh/" + keys.front().name + ".pub to authorized_keys)");
}

// makes sure there is a valid token
std::optional<std::string> ensureToken(RemoteClient::Impl& impl, bool allowPrompt) {
	if (impl.tokenValid()) {
		return std::nullopt;
	}
	const AuthOutcome outcome = authenticate(impl);
	switch (outcome.kind) {
		case AuthOutcome::Kind::Token: {
			std::scoped_lock lock(impl.mutex);
			impl.token = outcome.token;
			impl.tokenExpires = tokenExpiry(outcome.token);
			impl.passphraseFresh = false;
			return std::nullopt;
		}
		case AuthOutcome::Kind::NeedPassphrase:
			if (allowPrompt) {
				impl.setState(RemoteClient::State::NeedPassphrase, {}, outcome.keyName);
				return std::string();
			}
			return "session expired, reopen remote logs";
		case AuthOutcome::Kind::Error: return outcome.message;
	}
	return "unexpected state";
}

void loadList(const std::shared_ptr<RemoteClient::Impl>& impl) {
	impl->setState(RemoteClient::State::Loading);
	std::vector<RemoteLog> logs;
	for (int page = 1; page <= kMaxPages; ++page) {
		if (impl->cancel) {
			return;
		}
		const auto result = net::httpGet(impl->server + "/api/collections/logs/records?sort=-started&perPage=200&page=" + std::to_string(page),
		                                 {{"Authorization: " + impl->bearer()}});
		if (!result.error.empty() || result.status != 200) {
			impl->setState(RemoteClient::State::Error, describe(result, "list sessions"));
			return;
		}
		const auto parsed = json::parse(result.body, nullptr, false);
		if (!parsed.is_object() || !parsed.contains("items") || !parsed["items"].is_array()) {
			break;
		}
		for (const auto& row : parsed["items"]) {
			if (auto log = parseRow(row)) {
				logs.push_back(std::move(*log));
			}
		}
		const int totalPages = parsed.contains("totalPages") && parsed["totalPages"].is_number_integer() ? parsed["totalPages"].get<int>() : 1;
		if (page >= totalPages || parsed["items"].empty()) {
			break;
		}
	}
	impl->onUi([logs = std::move(logs)](RemoteClient::Impl& self) mutable {
		self.snap.logs = std::move(logs);
		self.snap.state = RemoteClient::State::Ready;
		self.snap.error.clear();
	});
}

void openJob(const std::shared_ptr<RemoteClient::Impl>& impl) {
	if (impl->server.empty()) {
		impl->setState(RemoteClient::State::Error, "no log server configured (set KENZO_REMOTE_URL)");
		return;
	}
	if (!impl->tokenValid()) {
		impl->setState(RemoteClient::State::SigningIn);
	}
	if (const auto problem = ensureToken(*impl, true)) {
		if (!problem->empty()) {
			impl->setState(RemoteClient::State::Error, *problem);
		}
		return;
	}
	loadList(impl);
}

std::string utf8(const fs::path& path) {
	const auto text = path.u8string();
	return std::string(text.begin(), text.end());
}

}  // namespace

RemoteClient::RemoteClient(Post post, std::string serverUrl, fs::path sshDir, fs::path downloadDir) : impl_(std::make_shared<Impl>()) {
	impl_->post = std::move(post);
	impl_->server = trimSlashes(std::move(serverUrl));
	impl_->sshDir = std::move(sshDir);
	if (downloadDir.empty()) {
		downloadDir = remoteLogDir().value_or(fs::temp_directory_path() / "kenzo-remote");
	}
	impl_->downloadDir = std::move(downloadDir);
}

RemoteClient::~RemoteClient() {
	impl_->cancel = true;
	if (impl_->worker.joinable()) {
		impl_->worker.join();
	}
}

const RemoteClient::Snapshot& RemoteClient::snapshot() const {
	return impl_->snap;
}

void RemoteClient::open() {
	{
		std::scoped_lock lock(impl_->mutex);
		impl_->triesLeft = kPassphraseTries;
		impl_->keyIndex = 0;
		impl_->passphraseFresh = false;
	}
	impl_->snap.state = impl_->tokenValid() ? State::Loading : State::SigningIn;
	impl_->snap.error.clear();
	impl_->snap.triesLeft = kPassphraseTries;
	impl_->startWorker(openJob);
}

void RemoteClient::submitPassphrase(std::string passphrase) {
	{
		std::scoped_lock lock(impl_->mutex);
		impl_->passphrase = std::move(passphrase);
		impl_->passphraseFresh = true;
	}
	impl_->snap.state = State::SigningIn;
	impl_->snap.error.clear();
	impl_->startWorker(openJob);
}

void RemoteClient::cancelPassphrase() {
	impl_->cancel = true;
	if (impl_->worker.joinable()) {
		impl_->worker.join();
	}
	std::scoped_lock lock(impl_->mutex);
	impl_->passphrase.reset();
	impl_->passphraseFresh = false;
	impl_->snap.state = State::Idle;
	impl_->snap.error.clear();
}

void RemoteClient::cancelDownload() {
	impl_->cancel = true;
}

void RemoteClient::download(const RemoteLog& log, net::Progress progress, DownloadDone done) {
	impl_->startWorker([log, progress = std::move(progress), done = std::move(done)](const std::shared_ptr<Impl>& impl) {
		const auto finish = [&](std::expected<fs::path, std::string> outcome) {
			impl->onUi([done, outcome = std::move(outcome)](Impl&) { done(outcome); });
		};
		const auto report = [&](std::uint64_t done_bytes, std::uint64_t total) {
			if (progress) {
				impl->onUi([progress, done_bytes, total](Impl&) { progress(done_bytes, total); });
			}
		};

		std::error_code ec;
		fs::create_directories(impl->downloadDir, ec);
		const fs::path dest = impl->downloadDir / localFileName(log);
		const fs::path marker = fs::path(dest).concat(".done");
		if (fs::exists(dest, ec) && fs::exists(marker, ec)) {
			finish(dest);
			return;
		}
		if (const auto problem = ensureToken(*impl, false)) {
			finish(std::unexpected(*problem));
			return;
		}
		const std::string authHeader = "Authorization: " + impl->bearer();
		const auto fileToken = net::httpPostJson(impl->server + "/api/files/token", "", {{authHeader}});
		const auto fileTokenValue = fileToken.ok() ? stringField(fileToken.body, "token") : std::nullopt;
		if (!fileTokenValue) {
			finish(std::unexpected(describe(fileToken, "issue file tokens")));
			return;
		}

		std::ofstream out(dest, std::ios::binary | std::ios::trunc);
		if (!out) {
			finish(std::unexpected("cannot write " + utf8(dest)));
			return;
		}
		bool writeFailed = false;
		Gunzip gunzip([&](std::span<const std::byte> data) {
			out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
			writeFailed = !out;
			return !writeFailed;
		});
		const auto result = net::httpDownload(
		    impl->server + "/api/files/logs/" + log.id + "/" + log.file + "?token=" + *fileTokenValue, {{authHeader}},
		    [&](std::span<const std::byte> data) { return gunzip.write(data); }, report, &impl->cancel);
		out.close();

		std::string problem;
		if (result.error == "cancelled" && impl->cancel) {
			problem = "cancelled";
		} else if (writeFailed) {
			problem = "cannot write " + utf8(dest);
		} else if (!result.error.empty()) {
			problem = result.error == "cancelled" ? "downloaded data is not valid gzip" : "download failed: " + result.error;
		} else if (result.status < 200 || result.status >= 300) {
			problem = describe(result, "send the file");
		} else if (!gunzip.finish()) {
			problem = "download was incomplete";
		}
		if (!problem.empty()) {
			fs::remove(dest, ec);
			finish(std::unexpected(std::move(problem)));
			return;
		}
		std::ofstream(marker) << log.id << "\n";
		finish(dest);
	});
}

}  // namespace kenzo
