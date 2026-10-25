#include <zlib.h>

#include <rtxui/headless.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <expected>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <thread>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/format.hpp"
#include "core/gunzip.hpp"
#include "core/process.hpp"
#include "fake_http.hpp"
#include "net/http.hpp"
#include "net/remote.hpp"
#include "net/ssh_auth.hpp"
#include "ui/app.hpp"

namespace {

using kenzo::fake::FakeHttp;
using kenzo::fake::Reply;
using kenzo::fake::Request;

struct Check {
	const char* name;
	std::function<void()> run;
};

std::vector<Check>& registry() {
	static std::vector<Check> checks;
	return checks;
}

struct Register {
	Register(const char* name, std::function<void()> run) { registry().push_back({name, std::move(run)}); }
};

void check(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

#define CHECK(name) \
	void name(); \
	const Register register_##name(#name, name); \
	void name()

std::string gzip(const std::string& data) {
	z_stream stream {};
	check(deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) == Z_OK, "deflateInit2");
	stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
	stream.avail_in = static_cast<uInt>(data.size());
	std::string out;
	std::array<char, 65536> buffer {};
	int status = Z_OK;
	while (status != Z_STREAM_END) {
		stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
		stream.avail_out = static_cast<uInt>(buffer.size());
		status = deflate(&stream, Z_FINISH);
		out.append(buffer.data(), buffer.size() - stream.avail_out);
	}
	deflateEnd(&stream);
	return out;
}

std::span<const std::byte> bytes(const std::string& text, std::size_t from, std::size_t count) {
	return std::as_bytes(std::span(text.data() + from, count));
}

std::string sample(std::size_t size) {
	std::string text;
	text.reserve(size);
	for (std::size_t i = 0; text.size() < size; ++i) {
		text += "line " + std::to_string(i * 2654435761u) + "\n";
	}
	text.resize(size);
	return text;
}

CHECK(get_returns_status_and_body) {
	FakeHttp server([](const Request&) { return Reply {200, R"({"a":1})"}; });
	const auto result = kenzo::net::httpGet(server.url() + "/x");
	check(result.ok() && result.status == 200 && result.body == R"({"a":1})", "200 with body");
}

CHECK(get_sends_custom_headers) {
	FakeHttp server([](const Request&) { return Reply {200, "{}"}; });
	kenzo::net::httpGet(server.url() + "/x", {{"Authorization: Bearer T"}});
	const auto seen = server.requests();
	check(seen.size() == 1 && seen[0].header("Authorization") == "Bearer T", "header arrives");
}

CHECK(post_sends_json_body_and_type) {
	FakeHttp server([](const Request&) { return Reply {200, "{}"}; });
	const auto result = kenzo::net::httpPostJson(server.url() + "/auth", R"({"k":"v"})");
	const auto seen = server.requests();
	check(result.ok(), "ok");
	check(seen.size() == 1 && seen[0].method == "POST" && seen[0].body == R"({"k":"v"})", "body arrives");
	check(seen[0].header("Content-Type") == "application/json", "content type");
}

CHECK(error_status_is_reported_with_body) {
	FakeHttp server([](const Request&) { return Reply {500, "boom"}; });
	const auto result = kenzo::net::httpGet(server.url() + "/x");
	check(!result.ok() && result.status == 500 && result.body == "boom" && result.error.empty(), "500 keeps body, no transport error");
}

CHECK(closed_port_fails_fast_with_an_error) {
	std::uint16_t port = 0;
	{
		asio::io_context context;
		asio::ip::tcp::acceptor probe(context, asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
		port = probe.local_endpoint().port();
	}
	const auto begin = std::chrono::steady_clock::now();
	const auto result = kenzo::net::httpGet("http://127.0.0.1:" + std::to_string(port) + "/x");
	check(std::chrono::steady_clock::now() - begin < std::chrono::seconds(12), "returns within 12 s");
	check(result.status == 0 && !result.error.empty(), "transport error, no status");
}

CHECK(download_streams_with_monotone_progress) {
	const std::string payload = sample(3 * 1024 * 1024);
	FakeHttp server([&](const Request&) { return Reply {200, payload, "application/octet-stream"}; });
	std::string got;
	std::uint64_t last = 0;
	std::uint64_t total = 0;
	bool monotone = true;
	const auto result = kenzo::net::httpDownload(
	    server.url() + "/f", {},
	    [&](std::span<const std::byte> chunk) {
		    got.append(reinterpret_cast<const char*>(chunk.data()), chunk.size());
		    return true;
	    },
	    [&](std::uint64_t done, std::uint64_t all) {
		    monotone = monotone && done >= last;
		    last = done;
		    total = all;
	    });
	check(result.ok() && got == payload, "all bytes arrive intact");
	check(monotone && total == payload.size() && last == payload.size(), "progress ends at the total");
}

CHECK(download_cancel_flag_stops_the_transfer) {
	FakeHttp server([&](const Request&) { return Reply {200, sample(8 * 1024 * 1024), "application/octet-stream"}; });
	std::atomic_bool cancel = false;
	std::size_t received = 0;
	const auto result = kenzo::net::httpDownload(
	    server.url() + "/f", {},
	    [&](std::span<const std::byte> chunk) {
		    received += chunk.size();
		    cancel = true;
		    return true;
	    },
	    {}, &cancel);
	check(result.error == "cancelled", "reported as cancelled");
	check(received < 8 * 1024 * 1024, "stopped early");
}

CHECK(download_sink_refusal_cancels) {
	FakeHttp server([&](const Request&) { return Reply {200, sample(1024 * 1024), "application/octet-stream"}; });
	const auto result = kenzo::net::httpDownload(server.url() + "/f", {}, [](std::span<const std::byte>) { return false; });
	check(result.error == "cancelled", "refusal reported as cancelled");
}

CHECK(download_error_status_keeps_body_and_skips_sink) {
	FakeHttp server([&](const Request&) { return Reply {404, R"({"message":"nope"})"}; });
	bool sinkCalled = false;
	const auto result = kenzo::net::httpDownload(server.url() + "/f", {}, [&](std::span<const std::byte>) {
		sinkCalled = true;
		return true;
	});
	check(!sinkCalled && result.status == 404 && result.body.find("nope") != std::string::npos, "404 handled");
}

CHECK(gunzip_round_trips_at_any_chunk_size) {
	const std::string source = sample(300000);
	const std::string packed = gzip(source);
	for (const std::size_t chunk : {std::size_t {1}, std::size_t {7}, std::size_t {65536}, packed.size()}) {
		std::string out;
		kenzo::Gunzip gunzip([&](std::span<const std::byte> data) {
			out.append(reinterpret_cast<const char*>(data.data()), data.size());
			return true;
		});
		for (std::size_t at = 0; at < packed.size(); at += chunk) {
			check(gunzip.write(bytes(packed, at, std::min(chunk, packed.size() - at))), "write");
		}
		check(gunzip.finish() && out == source, "identical output");
	}
}

CHECK(gunzip_flags_truncated_corrupt_and_empty_input) {
	const std::string packed = gzip(sample(100000));
	{
		kenzo::Gunzip gunzip([](std::span<const std::byte>) { return true; });
		check(gunzip.write(bytes(packed, 0, packed.size() / 2)), "half is accepted");
		check(!gunzip.finish(), "truncated stream does not finish");
	}
	{
		kenzo::Gunzip gunzip([](std::span<const std::byte>) { return true; });
		const std::string garbage = "this is not gzip data at all";
		check(!gunzip.write(bytes(garbage, 0, garbage.size())), "garbage is rejected");
	}
	{
		kenzo::Gunzip gunzip([](std::span<const std::byte>) { return true; });
		check(!gunzip.finish(), "no input is not a complete stream");
	}
	{
		kenzo::Gunzip gunzip([](std::span<const std::byte>) { return false; });
		check(!gunzip.write(bytes(packed, 0, packed.size())), "a refusing sink fails the write");
	}
}

// process and ssh signing

namespace fs = std::filesystem;

struct TempDir {
	fs::path path;
	TempDir() {
		path = fs::temp_directory_path() / ("kenzo_checks_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		fs::create_directories(path);
	}
	~TempDir() {
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};

// ed25519 key from the system ssh-keygen
bool makeKey(const fs::path& file, const std::string& passphrase, const std::string& type = "ed25519") {
	const auto run = kenzo::runProcess({"ssh-keygen", "-q", "-t", type, "-N", passphrase, "-f", file.string()}, "", {}, std::chrono::seconds(30));
	return run.started && run.exitCode == 0;
}

bool haveSshKeygen() {
	const auto run = kenzo::runProcess({"ssh-keygen", "-?"}, "", {}, std::chrono::seconds(10));
	return run.started;
}

std::string readFile(const fs::path& path) {
	std::ifstream in(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), {});
}

// checks the signature like the server hook does
bool signatureIsValid(const TempDir& dir, const std::string& challenge, const std::string& signature) {
	const fs::path sig = dir.path / "check.sig";
	std::ofstream(sig, std::ios::binary) << signature;
	const auto run = kenzo::runProcess({"ssh-keygen", "-Y", "check-novalidate", "-n", "kenzo", "-s", sig.string()}, challenge, {}, std::chrono::seconds(10));
	return run.started && run.exitCode == 0;
}

kenzo::net::SshKey keyAt(const fs::path& file) {
	kenzo::net::SshKey key;
	key.priv = file;
	key.pub = fs::path(file).concat(".pub");
	key.name = file.filename().string();
	return key;
}

CHECK(run_process_captures_output_and_exit_code) {
#ifdef _WIN32
	const auto run = kenzo::runProcess({"cmd", "/c", "echo", "hi"}, "", {}, std::chrono::seconds(10));
#else
	const auto run = kenzo::runProcess({"echo", "hi"}, "", {}, std::chrono::seconds(10));
#endif
	check(run.started && run.exitCode == 0, "ran and exited 0");
	check(run.out.find("hi") != std::string::npos, "stdout captured");
}

CHECK(run_process_reports_missing_program) {
	const auto run = kenzo::runProcess({"definitely-not-a-program-kenzo"}, "", {}, std::chrono::seconds(5));
	check(!run.started, "not started");
}

CHECK(run_process_times_out_and_kills) {
#ifdef _WIN32
	const std::vector<std::string> command = {"ping", "-n", "30", "127.0.0.1"};
#else
	const std::vector<std::string> command = {"sleep", "30"};
#endif
	const auto begin = std::chrono::steady_clock::now();
	const auto run = kenzo::runProcess(command, "", {}, std::chrono::milliseconds(400));
	check(run.timedOut, "timed out");
	check(std::chrono::steady_clock::now() - begin < std::chrono::seconds(5), "returned promptly");
}

CHECK(run_process_passes_stdin_and_env) {
#ifdef _WIN32
	const auto run = kenzo::runProcess({"cmd", "/c", "set KENZO_TEST_VALUE"}, "", {{"KENZO_TEST_VALUE", "abc123"}}, std::chrono::seconds(10));
	check(run.out.find("KENZO_TEST_VALUE=abc123") != std::string::npos, "env reaches the child");
#else
	const auto run = kenzo::runProcess({"sh", "-c", "echo $KENZO_TEST_VALUE"}, "", {{"KENZO_TEST_VALUE", "abc123"}}, std::chrono::seconds(10));
	check(run.out.find("abc123") != std::string::npos, "env reaches the child");
#endif
}

CHECK(find_ssh_keys_lists_ed25519_only_id_ed25519_first) {
	if (!haveSshKeygen()) {
		std::puts("skip (no ssh-keygen)");
		return;
	}
	TempDir dir;
	check(makeKey(dir.path / "zeta", ""), "make zeta");
	check(makeKey(dir.path / "id_ed25519", ""), "make id_ed25519");
	check(makeKey(dir.path / "id_rsa", "", "rsa"), "make rsa");
	std::ofstream(dir.path / "notes.pub") << "not a key\n";
	const auto keys = kenzo::net::findSshKeys(dir.path);
	check(keys.size() == 2, "rsa and junk are ignored");
	check(keys[0].name == "id_ed25519" && keys[1].name == "zeta", "id_ed25519 first, then by name");
	check(keys[0].priv == dir.path / "id_ed25519" && keys[0].pub == dir.path / "id_ed25519.pub", "paths");
	check(kenzo::net::findSshKeys(dir.path / "missing").empty(), "missing directory is empty");
}

CHECK(sign_challenge_with_an_unencrypted_key) {
	if (!haveSshKeygen()) {
		std::puts("skip (no ssh-keygen)");
		return;
	}
	TempDir dir;
	check(makeKey(dir.path / "k", ""), "make key");
	const std::string challenge = "1700000000000.deadbeef";
	const auto result = kenzo::net::signChallenge(keyAt(dir.path / "k"), challenge, std::nullopt);
	check(result.status == kenzo::net::SignStatus::Ok, "signs without a prompt");
	check(result.signature.starts_with("-----BEGIN SSH SIGNATURE-----"), "armored signature");
	check(signatureIsValid(dir, challenge, result.signature), "ssh-keygen accepts it");
	check(!signatureIsValid(dir, "another challenge", result.signature), "and rejects it for another message");
}

CHECK(sign_challenge_with_an_encrypted_key_uses_the_passphrase) {
	if (!haveSshKeygen()) {
		std::puts("skip (no ssh-keygen)");
		return;
	}
	TempDir dir;
	check(makeKey(dir.path / "k", "correct horse"), "make encrypted key");
	const std::string challenge = "1700000000001.cafe";
	const auto key = keyAt(dir.path / "k");
	check(kenzo::net::signChallenge(key, challenge, std::nullopt).status == kenzo::net::SignStatus::NeedPassphrase, "asks for a passphrase");
	check(kenzo::net::signChallenge(key, challenge, std::string("wrong")).status == kenzo::net::SignStatus::WrongPassphrase, "wrong passphrase");
	const auto ok = kenzo::net::signChallenge(key, challenge, std::string("correct horse"));
	check(ok.status == kenzo::net::SignStatus::Ok, "right passphrase signs");
	check(signatureIsValid(dir, challenge, ok.signature), "and the signature is valid");
}

CHECK(sign_challenge_reports_missing_key_files) {
	if (!haveSshKeygen()) {
		std::puts("skip (no ssh-keygen)");
		return;
	}
	TempDir dir;
	const auto result = kenzo::net::signChallenge(keyAt(dir.path / "nope"), "c", std::nullopt);
	check(result.status == kenzo::net::SignStatus::Failed, "failed, not a crash");
}

CHECK(askpass_prints_the_passphrase_from_the_environment) {
	std::string out;
#ifdef _WIN32
	_putenv_s("KENZO_ASKPASS", "1");
	_putenv_s("KENZO_PASSPHRASE", "s3cret");
#else
	setenv("KENZO_ASKPASS", "1", 1);
	setenv("KENZO_PASSPHRASE", "s3cret", 1);
#endif
	check(kenzo::net::isAskpassInvocation(), "recognised");
	const auto run = kenzo::runProcess({KENZO_EXE_PATH, "Enter passphrase:"}, "", {{"KENZO_ASKPASS", "1"}, {"KENZO_PASSPHRASE", "s3cret"}}, std::chrono::seconds(10));
	check(run.exitCode == 0 && run.out.find("s3cret") != std::string::npos, "the real kenzo exe answers as askpass");
	const auto none = kenzo::runProcess({KENZO_EXE_PATH, "Enter passphrase:"}, "", {{"KENZO_ASKPASS", "1"}, {"KENZO_PASSPHRASE", ""}}, std::chrono::seconds(10));
	check(none.exitCode != 0, "no passphrase means failure");
}

// remote client against a fake server

using kenzo::RemoteClient;
using State = kenzo::RemoteClient::State;

std::string base64url(const std::string& data) {
	static constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
	std::string out;
	for (std::size_t i = 0; i < data.size(); i += 3) {
		const std::uint32_t chunk = (static_cast<std::uint8_t>(data[i]) << 16) | (i + 1 < data.size() ? static_cast<std::uint8_t>(data[i + 1]) << 8 : 0) |
		                            (i + 2 < data.size() ? static_cast<std::uint8_t>(data[i + 2]) : 0);
		out.push_back(alphabet[(chunk >> 18) & 63]);
		out.push_back(alphabet[(chunk >> 12) & 63]);
		if (i + 1 < data.size()) out.push_back(alphabet[(chunk >> 6) & 63]);
		if (i + 2 < data.size()) out.push_back(alphabet[chunk & 63]);
	}
	return out;
}

std::string fakeJwt(std::int64_t expiresInSeconds) {
	const auto exp = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count() + expiresInSeconds;
	return base64url(R"({"alg":"HS256"})") + "." + base64url("{\"exp\":" + std::to_string(exp) + "}") + ".sig";
}

// fake server
struct FakeLogServer {
	std::vector<std::string> allowedPubLines;
	std::string rows = R"([])";
	std::string fileBytes;
	std::vector<std::string> pagedRows;
	bool paged = false;
	std::atomic<int> tokenLifetime = 1800;
	// holds the file back so the download stays visible
	std::chrono::milliseconds fileDelay {0};
	TempDir work;
	std::unique_ptr<FakeHttp> http;

	explicit FakeLogServer(std::string jsonRows = "[]") : rows(std::move(jsonRows)) {
		http = std::make_unique<FakeHttp>([this](const Request& request) { return answer(request); });
	}

	[[nodiscard]] std::string url() const { return http->url(); }

	Reply answer(const Request& request) {
		if (request.target == "/kenzo/challenge") {
			return {200, R"({"challenge":"1700000000000.cafe"})"};
		}
		if (request.target == "/kenzo/auth") {
			const auto body = nlohmann::json::parse(request.body, nullptr, false);
			if (!body.is_object() || !body.contains("challenge") || !body.contains("signature")) {
				return {400, R"({"error":"bad body"})"};
			}
			std::string allowed;
			for (const auto& line : allowedPubLines) {
				allowed += "kenzo namespaces=\"kenzo\" " + line + "\n";
			}
			std::ofstream(work.path / "allowed", std::ios::binary) << allowed;
			std::ofstream(work.path / "sig", std::ios::binary) << body["signature"].get<std::string>();
			const auto run = kenzo::runProcess({"ssh-keygen", "-Y", "verify", "-f", (work.path / "allowed").string(), "-I", "kenzo", "-n", "kenzo", "-s",
			                                     (work.path / "sig").string()},
			                                    body["challenge"].get<std::string>(), {}, std::chrono::seconds(10));
			if (run.started && run.exitCode == 0) {
				return {200, "{\"token\":\"" + fakeJwt(tokenLifetime) + "\"}"};
			}
			return {401, R"({"error":"not whitelisted"})"};
		}
		if (request.target.starts_with("/api/collections/logs/records")) {
			if (paged) {
				const auto numberAfter = [&](const std::string& key, int fallback) {
					const auto at = request.target.find(key);
					return at == std::string::npos ? fallback : std::atoi(request.target.c_str() + at + key.size());
				};
				const int page = numberAfter("page=", 1);
				const int perPage = numberAfter("perPage=", 30);
				std::string items;
				for (int i = (page - 1) * perPage; i < page * perPage && i < static_cast<int>(pagedRows.size()); ++i) {
					items += (items.empty() ? "" : ",") + pagedRows[static_cast<std::size_t>(i)];
				}
				const int total = static_cast<int>(pagedRows.size());
				return {200, "{\"page\":" + std::to_string(page) + ",\"perPage\":" + std::to_string(perPage) + ",\"totalItems\":" + std::to_string(total) + ",\"totalPages\":" + std::to_string((total + perPage - 1) / perPage) + ",\"items\":[" + items + "]}"};
			}
			return {200, "{\"items\":" + rows + "}"};
		}
		if (request.target == "/api/files/token") {
			return {200, R"({"token":"FT"})"};
		}
		if (request.target.starts_with("/api/files/logs/")) {
			if (request.target.find("token=FT") == std::string::npos) {
				return {403, "{}"};
			}
			std::this_thread::sleep_for(fileDelay);
			return {200, fileBytes, "application/gzip"};
		}
		return {404, "{}"};
	}
};

std::string pubLine(const fs::path& key) {
	std::string line = readFile(fs::path(key).concat(".pub"));
	while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
		line.pop_back();
	}
	return line;
}

// runs queued ui tasks until done or timeout
struct Pump {
	std::mutex mutex;
	std::deque<std::function<void()>> tasks;

	kenzo::RemoteClient::Post post() {
		return [this](std::function<void()> task) {
			std::scoped_lock lock(mutex);
			tasks.push_back(std::move(task));
		};
	}

	bool until(const std::function<bool()>& done, std::chrono::seconds timeout = std::chrono::seconds(60)) {
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (std::chrono::steady_clock::now() < deadline) {
			std::function<void()> task;
			{
				std::scoped_lock lock(mutex);
				if (!tasks.empty()) {
					task = std::move(tasks.front());
					tasks.pop_front();
				}
			}
			if (task) {
				task();
			} else if (done()) {
				return true;
			} else {
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
		}
		return done();
	}
};

const char* const kTwoRows =
    R"([{"id":"a1","host":"alpha","user":"ana","file":"log_a1.csv.gz","started":"2026-10-25 12:00:00.000Z","duration_s":90,"size":1048576},
        {"id":"b2","host":"beta","user":"bo","file":"log_b2.csv.gz","started":"2026-10-24 08:30:00.000Z","duration_s":3600,"size":2048}])";

CHECK(remote_signs_in_and_lists_sessions) {
	if (!haveSshKeygen()) {
		std::puts("skip (no ssh-keygen)");
		return;
	}
	TempDir ssh;
	check(makeKey(ssh.path / "id_ed25519", ""), "make key");
	FakeLogServer server(kTwoRows);
	server.allowedPubLines = {pubLine(ssh.path / "id_ed25519")};
	Pump pump;
	RemoteClient client(pump.post(), server.url(), ssh.path, ssh.path / "dl");
	client.open();
	check(pump.until([&] { return client.snapshot().state == State::Ready; }), "reaches Ready");
	const auto& logs = client.snapshot().logs;
	check(logs.size() == 2, "two rows");
	check(logs[0].id == "a1" && logs[0].host == "alpha" && logs[0].user == "ana" && logs[0].size == 1048576, "first row fields");
	check(logs[0].duration == std::chrono::seconds(90) && logs[1].duration == std::chrono::seconds(3600), "durations");
	const auto expected = std::chrono::sys_days(std::chrono::year(2026) / 10 / 25) + std::chrono::hours(12);
	check(logs[0].started == expected, "started parsed as UTC");
	check(logs[1].id == "b2", "order as served");
}

CHECK(remote_skips_a_rejected_key_and_uses_the_next) {
	if (!haveSshKeygen()) {
		std::puts("skip (no ssh-keygen)");
		return;
	}
	TempDir ssh;
	check(makeKey(ssh.path / "id_ed25519", ""), "make first key");
	check(makeKey(ssh.path / "second", ""), "make second key");
	FakeLogServer server(kTwoRows);
	server.allowedPubLines = {pubLine(ssh.path / "second")};
	Pump pump;
	RemoteClient client(pump.post(), server.url(), ssh.path, ssh.path / "dl");
	client.open();
	check(pump.until([&] { return client.snapshot().state == State::Ready; }), "second key gets in");
	check(server.http->count("/kenzo/auth") == 2, "both keys were tried");
}

CHECK(remote_reports_when_no_key_is_whitelisted) {
	if (!haveSshKeygen()) {
		std::puts("skip (no ssh-keygen)");
		return;
	}
	TempDir ssh;
	check(makeKey(ssh.path / "id_ed25519", ""), "make key");
	FakeLogServer server;
	Pump pump;
	RemoteClient client(pump.post(), server.url(), ssh.path, ssh.path / "dl");
	client.open();
	check(pump.until([&] { return client.snapshot().state == State::Error; }), "reaches Error");
	check(client.snapshot().error.find("no whitelisted key") != std::string::npos, "says so");
	check(client.snapshot().error.find("id_ed25519.pub") != std::string::npos, "names the key to add");
}

CHECK(remote_reports_when_there_is_no_key_at_all) {
	TempDir ssh;
	FakeLogServer server;
	Pump pump;
	RemoteClient client(pump.post(), server.url(), ssh.path, ssh.path / "dl");
	client.open();
	check(pump.until([&] { return client.snapshot().state == State::Error; }), "reaches Error");
	check(client.snapshot().error.find("no ed25519 key") != std::string::npos, "says so");
}

CHECK(remote_asks_for_the_passphrase_and_allows_three_tries) {
	if (!haveSshKeygen()) {
		std::puts("skip (no ssh-keygen)");
		return;
	}
	TempDir ssh;
	check(makeKey(ssh.path / "id_ed25519", "hunter2"), "make encrypted key");
	FakeLogServer server(kTwoRows);
	server.allowedPubLines = {pubLine(ssh.path / "id_ed25519")};
	Pump pump;
	RemoteClient client(pump.post(), server.url(), ssh.path, ssh.path / "dl");
	client.open();
	check(pump.until([&] { return client.snapshot().state == State::NeedPassphrase; }), "asks");
	check(client.snapshot().keyName == "id_ed25519" && client.snapshot().triesLeft == 3, "names the key, three tries");

	client.submitPassphrase("wrong");
	check(pump.until([&] { return client.snapshot().state == State::NeedPassphrase && client.snapshot().triesLeft == 2; }), "wrong costs a try");
	client.submitPassphrase("hunter2");
	check(pump.until([&] { return client.snapshot().state == State::Ready; }), "right passphrase gets in");
}

CHECK(remote_gives_up_after_three_wrong_passphrases) {
	if (!haveSshKeygen()) {
		std::puts("skip (no ssh-keygen)");
		return;
	}
	TempDir ssh;
	check(makeKey(ssh.path / "id_ed25519", "hunter2"), "make encrypted key");
	FakeLogServer server;
	server.allowedPubLines = {pubLine(ssh.path / "id_ed25519")};
	Pump pump;
	RemoteClient client(pump.post(), server.url(), ssh.path, ssh.path / "dl");
	client.open();
	check(pump.until([&] { return client.snapshot().state == State::NeedPassphrase; }), "asks");
	for (int tries = 0; tries < 3; ++tries) {
		client.submitPassphrase("nope");
		check(pump.until([&] { return client.snapshot().state != State::SigningIn; }), "answers");
	}
	check(client.snapshot().state == State::Error && client.snapshot().error.find("wrong passphrase") != std::string::npos, "error after three");
}

CHECK(remote_cancel_passphrase_returns_to_idle) {
	if (!haveSshKeygen()) {
		std::puts("skip (no ssh-keygen)");
		return;
	}
	TempDir ssh;
	check(makeKey(ssh.path / "id_ed25519", "hunter2"), "make encrypted key");
	FakeLogServer server;
	Pump pump;
	RemoteClient client(pump.post(), server.url(), ssh.path, ssh.path / "dl");
	client.open();
	check(pump.until([&] { return client.snapshot().state == State::NeedPassphrase; }), "asks");
	client.cancelPassphrase();
	check(client.snapshot().state == State::Idle, "idle again");
}

CHECK(remote_zero_rows_is_ready_and_empty) {
	if (!haveSshKeygen()) {
		std::puts("skip (no ssh-keygen)");
		return;
	}
	TempDir ssh;
	check(makeKey(ssh.path / "id_ed25519", ""), "make key");
	FakeLogServer server("[]");
	server.allowedPubLines = {pubLine(ssh.path / "id_ed25519")};
	Pump pump;
	RemoteClient client(pump.post(), server.url(), ssh.path, ssh.path / "dl");
	client.open();
	check(pump.until([&] { return client.snapshot().state == State::Ready; }), "Ready");
	check(client.snapshot().logs.empty(), "no rows");
}

CHECK(remote_skips_malformed_rows) {
	if (!haveSshKeygen()) {
		std::puts("skip (no ssh-keygen)");
		return;
	}
	TempDir ssh;
	check(makeKey(ssh.path / "id_ed25519", ""), "make key");
	FakeLogServer server(R"([{"id":"x"},{"id":"ok","host":"h","user":"u","file":"f.gz","started":"2026-10-25 12:00:00.000Z","duration_s":1,"size":2},
	                         {"id":"bad","host":"h","user":"u","file":"f.gz","started":"garbage","duration_s":1,"size":2}])");
	server.allowedPubLines = {pubLine(ssh.path / "id_ed25519")};
	Pump pump;
	RemoteClient client(pump.post(), server.url(), ssh.path, ssh.path / "dl");
	client.open();
	check(pump.until([&] { return client.snapshot().state == State::Ready; }), "Ready");
	check(client.snapshot().logs.size() == 1 && client.snapshot().logs[0].id == "ok", "only the valid row survives");
}

CHECK(remote_unreachable_server_is_a_clear_error) {
	TempDir ssh;
	std::uint16_t port = 0;
	{
		asio::io_context context;
		asio::ip::tcp::acceptor probe(context, asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
		port = probe.local_endpoint().port();
	}
	Pump pump;
	RemoteClient client(pump.post(), "http://127.0.0.1:" + std::to_string(port), ssh.path, ssh.path / "dl");
	client.open();
	check(pump.until([&] { return client.snapshot().state == State::Error; }), "Error");
	check(client.snapshot().error == "can't reach log server", "plain message");
}

CHECK(remote_downloads_gunzips_and_remembers) {
	if (!haveSshKeygen()) {
		std::puts("skip (no ssh-keygen)");
		return;
	}
	TempDir ssh;
	check(makeKey(ssh.path / "id_ed25519", ""), "make key");
	const std::string csv = sample(200000);
	FakeLogServer server(kTwoRows);
	server.fileBytes = gzip(csv);
	server.allowedPubLines = {pubLine(ssh.path / "id_ed25519")};
	Pump pump;
	RemoteClient client(pump.post(), server.url(), ssh.path, ssh.path / "dl");
	client.open();
	check(pump.until([&] { return client.snapshot().state == State::Ready; }), "Ready");

	std::optional<std::expected<fs::path, std::string>> outcome;
	std::uint64_t lastProgress = 0;
	client.download(client.snapshot().logs[0], [&](std::uint64_t done, std::uint64_t) { lastProgress = done; },
	                [&](std::expected<fs::path, std::string> result) { outcome = std::move(result); });
	check(pump.until([&] { return outcome.has_value(); }), "download finishes");
	check(outcome->has_value(), "succeeds");
	const fs::path file = **outcome;
	check(file.extension() == ".csv" && file.parent_path() == ssh.path / "dl", "a .csv in the download directory");
	check(file.filename().string().find("alpha") != std::string::npos && file.filename().string().find("ana") != std::string::npos, "named after host and user");
	check(readFile(file) == csv, "decompressed contents");

	outcome.reset();
	const auto fileRequests = server.http->count("/api/files/logs/");
	client.download(client.snapshot().logs[0], {}, [&](std::expected<fs::path, std::string> result) { outcome = std::move(result); });
	check(pump.until([&] { return outcome.has_value(); }), "second download finishes");
	check(outcome->has_value() && **outcome == file, "same file");
	check(server.http->count("/api/files/logs/") == fileRequests, "no second transfer");
}

CHECK(remote_download_failures_leave_no_file) {
	if (!haveSshKeygen()) {
		std::puts("skip (no ssh-keygen)");
		return;
	}
	TempDir ssh;
	check(makeKey(ssh.path / "id_ed25519", ""), "make key");
	FakeLogServer server(kTwoRows);
	server.allowedPubLines = {pubLine(ssh.path / "id_ed25519")};
	Pump pump;
	RemoteClient client(pump.post(), server.url(), ssh.path, ssh.path / "dl");
	client.open();
	check(pump.until([&] { return client.snapshot().state == State::Ready; }), "Ready");

	const std::string packed = gzip(sample(100000));
	for (const std::string& broken : {packed.substr(0, packed.size() / 2), std::string("not gzip at all"), std::string()}) {
		server.fileBytes = broken;
		std::optional<std::expected<fs::path, std::string>> outcome;
		client.download(client.snapshot().logs[0], {}, [&](std::expected<fs::path, std::string> result) { outcome = std::move(result); });
		check(pump.until([&] { return outcome.has_value(); }), "download ends");
		check(!outcome->has_value() && !outcome->error().empty(), "reported as an error");
		bool leftover = false;
		std::error_code ec;
		if (fs::exists(ssh.path / "dl", ec)) {
			for (const auto& entry : fs::directory_iterator(ssh.path / "dl")) {
				leftover = leftover || entry.path().extension() == ".csv" || entry.path().extension() == ".done";
			}
		}
		check(!leftover, "no partial csv or marker is left");
	}
}

CHECK(remote_cancel_download_leaves_no_file) {
	if (!haveSshKeygen()) {
		std::puts("skip (no ssh-keygen)");
		return;
	}
	TempDir ssh;
	check(makeKey(ssh.path / "id_ed25519", ""), "make key");
	FakeLogServer server(kTwoRows);
	server.fileBytes = gzip(sample(40 * 1024 * 1024));
	server.allowedPubLines = {pubLine(ssh.path / "id_ed25519")};
	Pump pump;
	RemoteClient client(pump.post(), server.url(), ssh.path, ssh.path / "dl");
	client.open();
	check(pump.until([&] { return client.snapshot().state == State::Ready; }), "Ready");

	std::optional<std::expected<fs::path, std::string>> outcome;
	bool cancelled = false;
	client.download(
	    client.snapshot().logs[0],
	    [&](std::uint64_t done, std::uint64_t) {
		    if (done > 0 && !cancelled) {
			    cancelled = true;
			    client.cancelDownload();
		    }
	    },
	    [&](std::expected<fs::path, std::string> result) { outcome = std::move(result); });
	check(pump.until([&] { return outcome.has_value(); }), "download ends");
	check(!outcome->has_value() && outcome->error() == "cancelled", "reported as cancelled");
	bool leftover = false;
	std::error_code ec;
	if (fs::exists(ssh.path / "dl", ec)) {
		for (const auto& entry : fs::directory_iterator(ssh.path / "dl")) {
			leftover = leftover || entry.path().extension() == ".csv";
		}
	}
	check(!leftover, "no partial csv");
}

// remote logs menu entry and popups

struct UiHarness {
	std::string server;
	fs::path sshDir;
	fs::path downloadDir;
	rtxui::Ref<kenzo::ui::App> app;
	std::unique_ptr<rtxui::TimelineScreen> screen;

	UiHarness(std::string serverUrl, fs::path ssh, fs::path downloads, int width = 120) : server(std::move(serverUrl)), sshDir(std::move(ssh)), downloadDir(std::move(downloads)) {
#ifdef _WIN32
		_putenv_s("RTXUI_HEADLESS", (std::to_string(width) + "x30").c_str());
		_putenv_s("RTXUI_STRICT", "1");
#else
		setenv("RTXUI_HEADLESS", (std::to_string(width) + "x30").c_str(), 1);
		setenv("RTXUI_STRICT", "1", 1);
#endif
		kenzo::ui::Options options;
		options.reducedMotion = true;
		options.remoteFactory = [this](kenzo::RemoteClient::Post post) {
			return std::make_unique<kenzo::RemoteClient>(std::move(post), server, sshDir, downloadDir);
		};
		app = rtxui::Ref<kenzo::ui::App>::New(options);
		screen = std::make_unique<rtxui::TimelineScreen>(app, width, 30);
		settle();
	}

	void settle(int frames = 10) {
		for (int i = 0; i < frames; ++i) {
			screen->Advance(16);
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
	}
	void input(std::string_view bytes) {
		screen->Input(bytes);
		settle();
	}
	[[nodiscard]] std::string text() { return screen->Text(); }
	bool waitFor(const std::function<bool(const std::string&)>& predicate, std::chrono::seconds timeout = std::chrono::seconds(30)) {
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (std::chrono::steady_clock::now() < deadline) {
			settle(5);
			if (predicate(text())) {
				return true;
			}
		}
		return predicate(text());
	}
	bool waitForText(const std::string& needle, std::chrono::seconds timeout = std::chrono::seconds(30)) {
		return waitFor([&](const std::string& frame) { return frame.find(needle) != std::string::npos; }, timeout);
	}
	void openRemoteLogs() {
		input("G");
		input("\r");
	}
};

bool contains(const std::string& haystack, const std::string& needle) {
	return haystack.find(needle) != std::string::npos;
}

CHECK(ui_menu_lists_remote_logs_last) {
	TempDir ssh;
	FakeLogServer server;
	UiHarness ui(server.url(), ssh.path, ssh.path / "dl");
	const std::string frame = ui.text();
	check(contains(frame, "remote logs"), "menu has the entry");
	check(frame.find("remote logs") > frame.find("more sessions"), "after more sessions");
}

CHECK(ui_popup_shows_columns_filters_and_opens_a_download) {
	if (!haveSshKeygen()) {
		std::puts("skip (no ssh-keygen)");
		return;
	}
	TempDir ssh;
	check(makeKey(ssh.path / "id_ed25519", ""), "make key");
	FakeLogServer server(kTwoRows);
	server.allowedPubLines = {pubLine(ssh.path / "id_ed25519")};
	server.fileBytes = gzip("1792684753419544000,INFO,engine.cpp,10,Engine,Remote hello world,,\n1792684753519544000,WARNING,assets.cpp,88,Assets,Missing texture,,\n");
	UiHarness ui(server.url(), ssh.path, ssh.path / "dl");
	ui.openRemoteLogs();
	check(ui.waitForText("alpha"), "rows appear");
	const std::string frame = ui.text();
	const std::vector<std::string> expected = {"alpha", "ana", "beta", "bo", kenzo::fmt::fileSize(1048576), kenzo::fmt::fileSize(2048), kenzo::fmt::duration(std::chrono::seconds(90)),
	                                           kenzo::fmt::duration(std::chrono::seconds(3600))};
	for (const std::string& needle : expected) {
		check(contains(frame, needle), needle.c_str());
	}

	ui.input("/");
	ui.input("bet");
	ui.input("\r");
	check(!contains(ui.text(), "alpha") && contains(ui.text(), "beta"), "typing filters by host");

	ui.input("\r");
	check(ui.waitForText("Remote hello world"), "the downloaded session opens in the table");
}

CHECK(ui_popup_masks_the_passphrase_and_counts_tries) {
	if (!haveSshKeygen()) {
		std::puts("skip (no ssh-keygen)");
		return;
	}
	TempDir ssh;
	check(makeKey(ssh.path / "id_ed25519", "hunter2"), "make encrypted key");
	FakeLogServer server(kTwoRows);
	server.allowedPubLines = {pubLine(ssh.path / "id_ed25519")};
	UiHarness ui(server.url(), ssh.path, ssh.path / "dl");
	ui.openRemoteLogs();
	check(ui.waitForText("passphrase for id_ed25519"), "prompt appears");
	check(contains(ui.text(), "3 tries left"), "three tries");

	ui.input("wrongpw");
	check(contains(ui.text(), "\xE2\x80\xA2") && !contains(ui.text(), "wrongpw"), "bullets, not letters");
	ui.input("\r");
	check(ui.waitForText("2 tries left"), "a wrong passphrase costs a try");
	check(!contains(ui.text(), "wrongpw"), "nothing leaks after submit");

	ui.input("hunter2");
	check(!contains(ui.text(), "hunter2"), "still masked");
	ui.input("\r");
	check(ui.waitForText("alpha"), "the right passphrase opens the list");
}

CHECK(ui_passphrase_escape_returns_to_the_menu) {
	if (!haveSshKeygen()) {
		std::puts("skip (no ssh-keygen)");
		return;
	}
	TempDir ssh;
	check(makeKey(ssh.path / "id_ed25519", "hunter2"), "make encrypted key");
	FakeLogServer server;
	UiHarness ui(server.url(), ssh.path, ssh.path / "dl");
	ui.openRemoteLogs();
	check(ui.waitForText("passphrase for"), "prompt appears");
	ui.input("\x1b");
	check(ui.waitFor([](const std::string& frame) { return !frame.contains("passphrase for"); }), "prompt closes");
	check(contains(ui.text(), "remote logs") && contains(ui.text(), "localhost"), "back at the menu");
}

CHECK(ui_popup_reports_an_unreachable_server_and_an_empty_list) {
	TempDir ssh;
	std::uint16_t port = 0;
	{
		asio::io_context context;
		asio::ip::tcp::acceptor probe(context, asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
		port = probe.local_endpoint().port();
	}
	UiHarness down("http://127.0.0.1:" + std::to_string(port), ssh.path, ssh.path / "dl");
	down.openRemoteLogs();
	check(down.waitForText("can't reach log server"), "unreachable server is explained");
	check(contains(down.text(), "r retry"), "and retry is offered");

	if (!haveSshKeygen()) {
		return;
	}
	check(makeKey(ssh.path / "id_ed25519", ""), "make key");
	FakeLogServer server("[]");
	server.allowedPubLines = {pubLine(ssh.path / "id_ed25519")};
	UiHarness empty(server.url(), ssh.path, ssh.path / "dl");
	empty.openRemoteLogs();
	check(empty.waitForText("no sessions uploaded yet"), "empty list has a message");
}

CHECK(ui_popup_keeps_long_names_inside_the_pane) {
	if (!haveSshKeygen()) {
		std::puts("skip (no ssh-keygen)");
		return;
	}
	TempDir ssh;
	check(makeKey(ssh.path / "id_ed25519", ""), "make key");
	const std::string longName(60, 'x');
	FakeLogServer server("[{\"id\":\"a1\",\"host\":\"" + longName + "\",\"user\":\"" + longName + R"(","file":"f.gz","started":"2026-10-25 12:00:00.000Z","duration_s":90,"size":1048576}])");
	server.allowedPubLines = {pubLine(ssh.path / "id_ed25519")};
	UiHarness ui(server.url(), ssh.path, ssh.path / "dl", 100);
	ui.openRemoteLogs();
	check(ui.waitForText("xxxx"), "row appears");
	std::size_t widest = 0;
	std::size_t lineStart = 0;
	const std::string frame = ui.text();
	while (lineStart <= frame.size()) {
		const auto end = frame.find('\n', lineStart);
		const std::string line = frame.substr(lineStart, end == std::string::npos ? std::string::npos : end - lineStart);
		std::size_t cells = 0;
		for (const unsigned char c : line) {
			cells += (c & 0xC0) != 0x80;
		}
		widest = std::max(widest, cells);
		if (end == std::string::npos) {
			break;
		}
		lineStart = end + 1;
	}
	check(widest <= 100, "no line is wider than the terminal");
	check(!contains(frame, std::string(60, 'x')), "the 60-character names are truncated");
}

// optional end to end check

CHECK(real_server_end_to_end) {
	const char* url = std::getenv("KENZO_E2E_URL");
	const char* sshDir = std::getenv("KENZO_E2E_SSH_DIR");
	const char* csvPath = std::getenv("KENZO_E2E_CSV");
	if (url == nullptr || sshDir == nullptr || csvPath == nullptr) {
		std::puts("skip (set KENZO_E2E_URL, KENZO_E2E_SSH_DIR and KENZO_E2E_CSV)");
		return;
	}
	TempDir downloads;
	Pump pump;
	RemoteClient client(pump.post(), url, sshDir, downloads.path);
	client.open();
	check(pump.until([&] { return client.snapshot().state == State::Ready || client.snapshot().state == State::Error; }), "finishes signing in");
	check(client.snapshot().state == State::Ready, ("signed in: " + client.snapshot().error).c_str());
	check(!client.snapshot().logs.empty(), "the uploaded session is listed");
	const auto& first = client.snapshot().logs[0];
	check(first.host == "e2ehost" && first.user == "e2euser", "host and user as uploaded");
	check(first.duration == std::chrono::seconds(7), "duration as uploaded");

	std::optional<std::expected<fs::path, std::string>> outcome;
	client.download(first, {}, [&](std::expected<fs::path, std::string> result) { outcome = std::move(result); });
	check(pump.until([&] { return outcome.has_value(); }), "download finishes");
	check(outcome->has_value(), outcome->has_value() ? "" : outcome->error().c_str());
	check(readFile(**outcome) == readFile(csvPath), "the downloaded CSV equals the uploaded one");
	check(first.size == fs::file_size(csvPath), "size as uploaded");
}

// edge cases

std::string u8(const fs::path& path) {
	const auto text = path.u8string();
	return std::string(text.begin(), text.end());
}

struct UnicodeDir {
	fs::path path;
	UnicodeDir() {
		path = fs::temp_directory_path() / fs::path(u8"kenzo_ключ_日本_").concat(std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		fs::create_directories(path);
	}
	~UnicodeDir() {
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};

CHECK(sign_challenge_works_in_a_non_ascii_profile_path) {
	if (!haveSshKeygen()) {
		std::puts("skip (no ssh-keygen)");
		return;
	}
	UnicodeDir dir;
	const auto makeResult = kenzo::runProcess({"ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", u8(dir.path / "id_ed25519")}, "", {}, std::chrono::seconds(30));
	check(makeResult.started && makeResult.exitCode == 0, "make key in a non-ASCII directory");
	const auto keys = kenzo::net::findSshKeys(dir.path);
	check(keys.size() == 1, "the key is found");
	const auto result = kenzo::net::signChallenge(keys[0], "1700000000000.beef", std::nullopt);
	check(result.status == kenzo::net::SignStatus::Ok, "signs from a path ANSI cannot represent");
}

#ifdef _WIN32
CHECK(default_ssh_dir_reads_a_non_ascii_user_profile) {
	const wchar_t* previous = _wgetenv(L"USERPROFILE");
	const std::wstring saved = previous != nullptr ? previous : L"";
	_wputenv_s(L"USERPROFILE", L"C:\\Users\\Jos\u00e9\u65e5\u672c");
	const fs::path dir = kenzo::net::defaultSshDir();
	_wputenv_s(L"USERPROFILE", saved.c_str());
	check(dir == fs::path(L"C:\\Users\\Jos\u00e9\u65e5\u672c") / ".ssh", "wide environment is used");
}
#endif

CHECK(openssh_gate_matches_the_askpass_require_release) {
	using kenzo::net::opensshSupportsAskpassRequire;
	check(opensshSupportsAskpassRequire("OpenSSH_for_Windows_9.5p2, LibreSSL 3.8.2"), "windows 9.5");
	check(opensshSupportsAskpassRequire("OpenSSH_8.4p1 Debian-5"), "8.4 is the first with SSH_ASKPASS_REQUIRE");
	check(opensshSupportsAskpassRequire("OpenSSH_10.3p1, OpenSSL 3.5.7"), "two-digit major");
	check(!opensshSupportsAskpassRequire("OpenSSH_8.3p1 Ubuntu-1ubuntu0.2, OpenSSL 1.1.1f"), "8.3 is too old");
	check(!opensshSupportsAskpassRequire("OpenSSH_for_Windows_8.1p1, LibreSSL 3.0.2"), "windows 10's 8.1 is too old");
	check(!opensshSupportsAskpassRequire("OpenSSH_7.9p1"), "7.9 is too old");
	check(opensshSupportsAskpassRequire("something unexpected"), "an unknown format does not block signing");
}

CHECK(remote_lists_every_page) {
	if (!haveSshKeygen()) {
		std::puts("skip (no ssh-keygen)");
		return;
	}
	TempDir ssh;
	check(makeKey(ssh.path / "id_ed25519", ""), "make key");
	FakeLogServer server;
	for (int i = 0; i < 450; ++i) {
		server.pagedRows.push_back("{\"id\":\"r" + std::to_string(i) + R"(","host":"h","user":"u","file":"f.gz","started":"2026-10-25 12:00:00.000Z","duration_s":1,"size":2})");
	}
	server.paged = true;
	server.allowedPubLines = {pubLine(ssh.path / "id_ed25519")};
	Pump pump;
	RemoteClient client(pump.post(), server.url(), ssh.path, ssh.path / "dl");
	client.open();
	check(pump.until([&] { return client.snapshot().state == State::Ready; }), "Ready");
	check(client.snapshot().logs.size() == 450, "all 450 rows, not just the first 200");
	check(client.snapshot().logs.front().id == "r0" && client.snapshot().logs.back().id == "r449", "in server order");
}

CHECK(remote_strips_control_characters_from_uploader_controlled_text) {
	if (!haveSshKeygen()) {
		std::puts("skip (no ssh-keygen)");
		return;
	}
	TempDir ssh;
	check(makeKey(ssh.path / "id_ed25519", ""), "make key");
	FakeLogServer server(
	    R"([{"id":"a","host":"\u001b[2Jevil\nx","user":"u\u0085v\u007f","file":"f.gz","started":"2026-10-25 12:00:00.000Z","duration_s":1,"size":2}])");
	server.allowedPubLines = {pubLine(ssh.path / "id_ed25519")};
	Pump pump;
	RemoteClient client(pump.post(), server.url(), ssh.path, ssh.path / "dl");
	client.open();
	check(pump.until([&] { return client.snapshot().state == State::Ready; }), "Ready");
	const auto& log = client.snapshot().logs.at(0);
	for (const std::string* text : {&log.host, &log.user}) {
		for (const unsigned char c : *text) {
			check(c >= 0x20 && c != 0x7f, "no control byte survives");
		}
		check(text->find("\xC2\x85") == std::string::npos, "no C1 control survives");
	}
	check(log.host.find("evil") != std::string::npos, "the readable part is kept");
}

CHECK(remote_without_a_configured_server_explains_itself) {
	TempDir ssh;
	Pump pump;
	RemoteClient client(pump.post(), "", ssh.path, ssh.path / "dl");
	client.open();
	check(pump.until([&] { return client.snapshot().state == State::Error; }, std::chrono::seconds(10)), "Error");
	check(client.snapshot().error.find("KENZO_REMOTE_URL") != std::string::npos, "names the setting");
}

CHECK(default_server_url_is_the_built_in_server_unless_overridden) {
#ifdef _WIN32
	_putenv_s("KENZO_REMOTE_URL", "");
#else
	unsetenv("KENZO_REMOTE_URL");
#endif
	check(kenzo::defaultServerUrl() == "http://kenzo.nullptr.es:8080", "the built-in server");
#ifdef _WIN32
	_putenv_s("KENZO_REMOTE_URL", "https://logs.example.org/");
#else
	setenv("KENZO_REMOTE_URL", "https://logs.example.org/", 1);
#endif
	check(kenzo::defaultServerUrl() == "https://logs.example.org", "env wins, trailing slash trimmed");
#ifdef _WIN32
	_putenv_s("KENZO_REMOTE_URL", "");
#else
	unsetenv("KENZO_REMOTE_URL");
#endif
}

// menu layout

void setConfigHome(const fs::path& dir) {
#ifdef _WIN32
	_putenv_s("APPDATA", u8(dir).c_str());
#else
	setenv("XDG_CONFIG_HOME", u8(dir).c_str(), 1);
#endif
}

std::string configHome() {
#ifdef _WIN32
	const char* value = std::getenv("APPDATA");
#else
	const char* value = std::getenv("XDG_CONFIG_HOME");
#endif
	return value != nullptr ? value : "";
}

CHECK(ui_menu_lists_five_recents_without_side_notes_and_one_blank_line) {
	TempDir home;
	const fs::path logs = home.path / "ToastEngine" / "logs";
	fs::create_directories(logs);
	for (int day = 1; day <= 7; ++day) {
		const std::string stem = "2026-10-0" + std::to_string(day) + "_10-00-00";
		std::ofstream(logs / (stem + ".csv"))
		    << "1792684753419544000,INFO,a.cpp,1,Engine,marker-from-0" << day << ",,\n1792684753519544000,INFO,a.cpp,2,Engine,second row,,\n";
	}
	const std::string saved = configHome();
	setConfigHome(home.path);
	{
		TempDir ssh;
		FakeLogServer server;
		UiHarness ui(server.url(), ssh.path, ssh.path / "dl");
		check(ui.waitFor([&](const std::string&) { return ui.app->menu_sessions.size() >= 5; }), "sessions are scanned");
		ui.settle(20);
		check(ui.app->menu_sessions.size() == 5, "exactly five recent sessions are listed, though seven exist");

		const std::string frame = ui.text();
		check(contains(frame, "more sessions") && contains(frame, "remote logs"), "both entries are still there");
		check(!contains(frame, "total"), "no 'N total' note after more sessions");
		check(!contains(frame, "uploaded"), "no 'N uploaded' note after remote logs");

		// lines between the last entry and the key hints
		std::vector<std::string> lines;
		std::size_t from = 0;
		while (from <= frame.size()) {
			const auto end = frame.find('\n', from);
			lines.push_back(frame.substr(from, end == std::string::npos ? std::string::npos : end - from));
			if (end == std::string::npos) {
				break;
			}
			from = end + 1;
		}
		std::size_t remoteLine = 0, hintLine = 0;
		for (std::size_t i = 0; i < lines.size(); ++i) {
			if (contains(lines[i], "remote logs")) remoteLine = i;
			if (contains(lines[i], "j/k move")) hintLine = i;
		}
		check(remoteLine > 0 && hintLine > remoteLine, "both lines found");
		check(hintLine - remoteLine == 2, "exactly one blank line between remote logs and the key hints");

		// key 5 opens the fifth and a sixth does nothing
		ui.input("6");
		check(contains(ui.text(), "remote logs"), "6 is not a shortcut, the menu stays");
		ui.input("5");
		check(ui.waitForText("marker-from-03"), "key 5 opens the fifth most recent session");
	}
	setConfigHome(saved);
}

CHECK(ui_download_progress_is_its_own_popup) {
	if (!haveSshKeygen()) {
		std::puts("skip (no ssh-keygen)");
		return;
	}
	TempDir ssh;
	check(makeKey(ssh.path / "id_ed25519", ""), "make key");
	FakeLogServer server(kTwoRows);
	server.allowedPubLines = {pubLine(ssh.path / "id_ed25519")};
	server.fileBytes = gzip("1792684753419544000,INFO,engine.cpp,10,Engine,Popup hello,,\n");
	server.fileDelay = std::chrono::milliseconds(4000);
	UiHarness ui(server.url(), ssh.path, ssh.path / "dl");
	ui.openRemoteLogs();
	check(ui.waitForText("alpha"), "rows appear");
	check(!ui.app->show_download, "no download popup before a download starts");

	ui.input("\r");
	check(ui.waitFor([&](const std::string&) { return ui.app->show_download; }), "the download popup appears");
	const std::string frame = ui.text();
	if (std::getenv("KENZO_PRINT_FRAMES") != nullptr) {
		std::puts(frame.c_str());
	}
	check(contains(frame, "Downloading"), "it has its own title");
	check(contains(frame, "alpha") && contains(frame, "ana"), "it names the session being fetched");
	check(contains(frame, "\xE2\x96\x91"), "it shows a progress bar");
	check(!ui.app->rem_show_status, "the list dialog no longer carries the progress line");

	ui.input("\x1b");
	check(ui.waitFor([&](const std::string&) { return !ui.app->show_download; }), "escape cancels the download and closes the popup");
	check(contains(ui.text(), "alpha") && contains(ui.text(), "Remote logs"), "the list is still there underneath");
	check(!contains(ui.text(), "cancelled"), "a cancel is not reported as an error");
}

}  // namespace

int main() {
	// as askpass helper this would recurse
	if (kenzo::net::isAskpassInvocation()) {
		return kenzo::net::askpassMain();
	}
	kenzo::net::g_askpassExe = KENZO_EXE_PATH;

	int failed = 0;
	for (const auto& entry : registry()) {
		try {
			entry.run();
			std::printf("ok   %s\n", entry.name);
		} catch (const std::exception& e) {
			std::printf("FAIL %s: %s\n", entry.name, e.what());
			++failed;
		}
	}
	std::printf("%zu checks, %d failed\n", registry().size(), failed);
	return failed == 0 ? 0 : 1;
}
