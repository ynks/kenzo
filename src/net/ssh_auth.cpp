#include "net/ssh_auth.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "core/paths.hpp"
#include "core/process.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace kenzo::net {
namespace {

namespace fs = std::filesystem;

constexpr auto kSignTimeout = std::chrono::seconds(10);

std::string readFirstLine(const fs::path& path) {
	std::ifstream in(path);
	std::string line;
	std::getline(in, line);
	return line;
}

bool sshSupportsAskpassRequire() {
	static const bool supported = [] {
		const auto run = runProcess({"ssh", "-V"}, "", {}, kSignTimeout);
		return !run.started || opensshSupportsAskpassRequire(run.err + run.out);
	}();
	return supported;
}

fs::path ownExecutable() {
#ifdef _WIN32
	std::wstring path(MAX_PATH, L'\0');
	for (;;) {
		const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
		if (length == 0) {
			return {};
		}
		if (length < path.size()) {
			path.resize(length);
			break;
		}
		path.resize(path.size() * 2);
	}
	// no spaces in the path since ssh-keygen runs it through a shell
	if (path.find(L' ') != std::wstring::npos) {
		std::wstring shortPath(MAX_PATH, L'\0');
		const DWORD length = GetShortPathNameW(path.c_str(), shortPath.data(), static_cast<DWORD>(shortPath.size()));
		if (length > 0 && length < shortPath.size()) {
			shortPath.resize(length);
			return shortPath;
		}
	}
	return path;
#else
	std::error_code ec;
	return fs::read_symlink("/proc/self/exe", ec);
#endif
}

std::map<std::string, std::string> askpassEnvironment(const std::optional<std::string>& passphrase) {
	const fs::path exe = g_askpassExe.empty() ? ownExecutable() : g_askpassExe;
	return {
	    {"SSH_ASKPASS", pathUtf8(exe)},
	    {"SSH_ASKPASS_REQUIRE", "force"},
	    {"KENZO_ASKPASS", "1"},
	    {"KENZO_PASSPHRASE", passphrase.value_or("")},
	};
}

bool looksTooOld(const ProcessResult& run) {
	return run.err.find("unknown option") != std::string::npos || run.err.find("illegal option") != std::string::npos;
}

bool signed_ok(const ProcessResult& run) {
	return run.started && !run.timedOut && run.exitCode == 0 && run.out.starts_with("-----BEGIN SSH SIGNATURE-----");
}

// a missing file counts as unencrypted
bool isEncrypted(const fs::path& priv) {
	std::error_code ec;
	if (!fs::exists(priv, ec)) {
		return false;
	}
	const auto run = runProcess({"ssh-keygen", "-y", "-P", "", "-f", pathUtf8(priv)}, "", {}, kSignTimeout);
	return run.started && run.exitCode != 0;
}

}  // namespace

fs::path defaultSshDir() {
#ifdef _WIN32
	// getenv mangles non ascii names
	const wchar_t* home = _wgetenv(L"USERPROFILE");
#else
	const char* home = std::getenv("HOME");
#endif
	return home != nullptr && *home != 0 ? fs::path(home) / ".ssh" : fs::path(".ssh");
}

bool opensshSupportsAskpassRequire(std::string_view text) {
	const auto at = text.find("OpenSSH_");
	if (at == std::string_view::npos) {
		return true;
	}
	auto rest = text.substr(at + 8);
	if (rest.starts_with("for_Windows_")) {
		rest.remove_prefix(12);
	}
	const char* const end = rest.data() + rest.size();
	int major = 0;
	int minor = 0;
	const auto [afterMajor, majorError] = std::from_chars(rest.data(), end, major);
	if (majorError != std::errc() || afterMajor == end || *afterMajor != '.') {
		return true;
	}
	if (std::from_chars(afterMajor + 1, end, minor).ec != std::errc()) {
		return true;
	}
	return major > 8 || (major == 8 && minor >= 4);
}

std::vector<SshKey> findSshKeys(const fs::path& sshDir) {
	std::vector<SshKey> keys;
	std::error_code ec;
	for (fs::directory_iterator it(sshDir, ec), end; !ec && it != end; it.increment(ec)) {
		const fs::path& pub = it->path();
		if (pub.extension() != ".pub" || !it->is_regular_file(ec)) {
			continue;
		}
		if (!readFirstLine(pub).starts_with("ssh-ed25519 ")) {
			continue;
		}
		SshKey key;
		key.pub = pub;
		key.priv = fs::path(pub).replace_extension();
		key.name = key.priv.filename().string();
		keys.push_back(std::move(key));
	}
	std::ranges::sort(keys, [](const SshKey& a, const SshKey& b) {
		const bool aFirst = a.name == "id_ed25519";
		const bool bFirst = b.name == "id_ed25519";
		return aFirst != bFirst ? aFirst : a.name < b.name;
	});
	return keys;
}

SignResult signChallenge(const SshKey& key, std::string_view challenge, const std::optional<std::string>& passphrase) {
	const auto env = askpassEnvironment(passphrase);
	const auto attempt = [&](const fs::path& file) {
		return runProcess({"ssh-keygen", "-Y", "sign", "-f", pathUtf8(file), "-n", "kenzo"}, challenge, env, kSignTimeout);
	};

	// the agent holds the private half
	ProcessResult run = attempt(key.pub);
	if (!run.started) {
		return {SignStatus::NoSshKeygen, {}};
	}
	if (signed_ok(run)) {
		return {SignStatus::Ok, run.out};
	}
	if (looksTooOld(run)) {
		return {SignStatus::TooOld, {}};
	}

	// askpass needs SSH_ASKPASS_REQUIRE past here
	if (!sshSupportsAskpassRequire()) {
		return {SignStatus::TooOld, {}};
	}

	std::error_code ec;
	if (fs::exists(key.priv, ec)) {
		run = attempt(key.priv);
		if (signed_ok(run)) {
			return {SignStatus::Ok, run.out};
		}
		if (isEncrypted(key.priv)) {
			return {passphrase ? SignStatus::WrongPassphrase : SignStatus::NeedPassphrase, {}};
		}
	}
	return {SignStatus::Failed, {}};
}

bool isAskpassInvocation() {
	const char* flag = std::getenv("KENZO_ASKPASS");
	return flag != nullptr && *flag != '\0';
}

int askpassMain() {
	const char* passphrase = std::getenv("KENZO_PASSPHRASE");
	if (passphrase == nullptr || *passphrase == '\0') {
		return 1;
	}
	std::puts(passphrase);
	return 0;
}

}  // namespace kenzo::net
