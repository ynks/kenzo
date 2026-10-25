#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kenzo::net {

struct SshKey {
	std::filesystem::path pub;
	std::filesystem::path priv;
	std::string name;
};

std::filesystem::path defaultSshDir();

// 8.4 is the first release with SSH_ASKPASS_REQUIRE
bool opensshSupportsAskpassRequire(std::string_view versionOutput);

std::vector<SshKey> findSshKeys(const std::filesystem::path& sshDir = defaultSshDir());

enum class SignStatus { Ok, NeedPassphrase, WrongPassphrase, NoSshKeygen, TooOld, Failed };

struct SignResult {
	SignStatus status = SignStatus::Failed;
	std::string signature;
};

inline std::filesystem::path g_askpassExe;

SignResult signChallenge(const SshKey& key, std::string_view challenge, const std::optional<std::string>& passphrase);

bool isAskpassInvocation();

int askpassMain();

}  // namespace kenzo::net
