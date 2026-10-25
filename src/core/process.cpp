#include "core/process.hpp"

#include <algorithm>
#include <array>
#include <cwchar>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdlib>
#endif

namespace kenzo {
namespace {

#ifdef _WIN32

std::wstring widen(const std::string& text) {
	if (text.empty()) {
		return {};
	}
	const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
	std::wstring out(static_cast<std::size_t>(size), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
	return out;
}

// quoting as CommandLineToArgvW reads it back
std::wstring quoteArg(const std::wstring& arg) {
	if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
		return arg;
	}
	std::wstring out = L"\"";
	for (auto it = arg.begin();; ++it) {
		std::size_t backslashes = 0;
		while (it != arg.end() && *it == L'\\') {
			++it;
			++backslashes;
		}
		if (it == arg.end()) {
			out.append(backslashes * 2, L'\\');
			break;
		}
		out.append(*it == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
		out.push_back(*it);
	}
	out.push_back(L'"');
	return out;
}

std::wstring environmentBlock(const std::map<std::string, std::string>& env) {
	std::vector<std::wstring> entries;
	const auto nameOf = [](const std::wstring& entry) { return entry.substr(0, entry.find(L'=', 1)); };
	std::vector<std::wstring> overridden;
	for (const auto& [name, value] : env) {
		overridden.push_back(widen(name));
		entries.push_back(widen(name) + L"=" + widen(value));
	}
	if (wchar_t* block = GetEnvironmentStringsW()) {
		for (const wchar_t* at = block; *at != L'\0'; at += std::wcslen(at) + 1) {
			std::wstring entry = at;
			const std::wstring name = nameOf(entry);
			const bool replaced = std::ranges::any_of(overridden, [&](const std::wstring& other) { return _wcsicmp(other.c_str(), name.c_str()) == 0; });
			if (!replaced) {
				entries.push_back(std::move(entry));
			}
		}
		FreeEnvironmentStringsW(block);
	}
	std::ranges::sort(entries, [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
	std::wstring joined;
	for (const auto& entry : entries) {
		joined += entry;
		joined.push_back(L'\0');
	}
	joined.push_back(L'\0');
	return joined;
}

struct Handle {
	HANDLE value = nullptr;
	Handle() = default;
	explicit Handle(HANDLE handle) : value(handle) {}
	Handle(const Handle&) = delete;
	Handle& operator=(const Handle&) = delete;
	~Handle() { close(); }
	void close() {
		if (value != nullptr && value != INVALID_HANDLE_VALUE) {
			CloseHandle(value);
		}
		value = nullptr;
	}
};

void drain(HANDLE pipe, std::string& into) {
	std::array<char, 4096> buffer {};
	DWORD got = 0;
	while (ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr) && got > 0) {
		into.append(buffer.data(), got);
	}
}

#endif

}  // namespace

#ifdef _WIN32

ProcessResult runProcess(const std::vector<std::string>& argv, std::string_view stdinData, const std::map<std::string, std::string>& env,
                         std::chrono::milliseconds timeout) {
	ProcessResult result;
	if (argv.empty()) {
		return result;
	}
	SECURITY_ATTRIBUTES inheritable {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
	Handle inRead, inWrite, outRead, outWrite, errRead, errWrite;
	if (!CreatePipe(&inRead.value, &inWrite.value, &inheritable, 0) || !CreatePipe(&outRead.value, &outWrite.value, &inheritable, 0) ||
	    !CreatePipe(&errRead.value, &errWrite.value, &inheritable, 0)) {
		return result;
	}
	SetHandleInformation(inWrite.value, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(outRead.value, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(errRead.value, HANDLE_FLAG_INHERIT, 0);

	std::wstring command;
	for (const auto& arg : argv) {
		command += (command.empty() ? L"" : L" ") + quoteArg(widen(arg));
	}
	std::wstring environment = environmentBlock(env);

	STARTUPINFOW startup {};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = inRead.value;
	startup.hStdOutput = outWrite.value;
	startup.hStdError = errWrite.value;
	PROCESS_INFORMATION process {};
	if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, environment.data(),
	                    nullptr, &startup, &process)) {
		return result;
	}
	Handle child(process.hProcess);
	Handle childThread(process.hThread);
	result.started = true;
	inRead.close();
	outWrite.close();
	errWrite.close();

	std::thread writer([&] {
		DWORD written = 0;
		for (std::size_t at = 0; at < stdinData.size();) {
			if (!WriteFile(inWrite.value, stdinData.data() + at, static_cast<DWORD>(stdinData.size() - at), &written, nullptr) || written == 0) {
				break;
			}
			at += written;
		}
		inWrite.close();
	});
	std::thread outReader([&] { drain(outRead.value, result.out); });
	std::thread errReader([&] { drain(errRead.value, result.err); });

	if (WaitForSingleObject(child.value, static_cast<DWORD>(timeout.count())) == WAIT_TIMEOUT) {
		result.timedOut = true;
		TerminateProcess(child.value, 1);
		WaitForSingleObject(child.value, 2000);
	}
	// a killed child breaks the pipes and frees the readers
	writer.join();
	outReader.join();
	errReader.join();
	DWORD code = 1;
	GetExitCodeProcess(child.value, &code);
	result.exitCode = static_cast<int>(code);
	return result;
}

#else

ProcessResult runProcess(const std::vector<std::string>& argv, std::string_view stdinData, const std::map<std::string, std::string>& env,
                         std::chrono::milliseconds timeout) {
	ProcessResult result;
	if (argv.empty()) {
		return result;
	}
	int in[2], out[2], err[2], status[2];
	if (pipe(in) != 0 || pipe(out) != 0 || pipe(err) != 0 || pipe(status) != 0) {
		return result;
	}
	fcntl(status[1], F_SETFD, FD_CLOEXEC);

	const pid_t pid = fork();
	if (pid < 0) {
		return result;
	}
	if (pid == 0) {
		setsid();  // no controlling terminal
		dup2(in[0], 0);
		dup2(out[1], 1);
		dup2(err[1], 2);
		for (int fd : {in[0], in[1], out[0], out[1], err[0], err[1], status[0]}) {
			close(fd);
		}
		for (const auto& [name, value] : env) {
			setenv(name.c_str(), value.c_str(), 1);
		}
		std::vector<char*> args;
		for (const auto& arg : argv) {
			args.push_back(const_cast<char*>(arg.c_str()));
		}
		args.push_back(nullptr);
		execvp(args[0], args.data());
		const int failure = errno;
		(void)!write(status[1], &failure, sizeof(failure));
		_exit(127);
	}
	close(in[0]);
	close(out[1]);
	close(err[1]);
	close(status[1]);

	int failure = 0;
	if (read(status[0], &failure, sizeof(failure)) > 0) {
		close(in[1]);
		close(out[0]);
		close(err[0]);
		close(status[0]);
		waitpid(pid, nullptr, 0);
		return result;
	}
	close(status[0]);
	result.started = true;

	fcntl(in[1], F_SETFL, O_NONBLOCK);
	std::size_t written = 0;
	if (stdinData.empty()) {
		close(in[1]);
		in[1] = -1;
	}
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	int outFd = out[0];
	int errFd = err[0];
	while (outFd >= 0 || errFd >= 0) {
		const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
		if (left.count() <= 0) {
			result.timedOut = true;
			kill(pid, SIGKILL);
			break;
		}
		std::vector<pollfd> fds;
		if (outFd >= 0) fds.push_back({outFd, POLLIN, 0});
		if (errFd >= 0) fds.push_back({errFd, POLLIN, 0});
		if (in[1] >= 0) fds.push_back({in[1], POLLOUT, 0});
		if (poll(fds.data(), fds.size(), static_cast<int>(left.count())) < 0 && errno != EINTR) {
			break;
		}
		for (const auto& entry : fds) {
			if (entry.revents == 0) {
				continue;
			}
			if (entry.fd == in[1]) {
				const ssize_t n = write(in[1], stdinData.data() + written, stdinData.size() - written);
				if (n > 0) written += static_cast<std::size_t>(n);
				if (written >= stdinData.size() || (n < 0 && errno != EAGAIN)) {
					close(in[1]);
					in[1] = -1;
				}
				continue;
			}
			std::array<char, 4096> buffer {};
			const ssize_t n = read(entry.fd, buffer.data(), buffer.size());
			if (n > 0) {
				(entry.fd == outFd ? result.out : result.err).append(buffer.data(), static_cast<std::size_t>(n));
			} else if (n == 0 || errno != EINTR) {
				close(entry.fd);
				(entry.fd == outFd ? outFd : errFd) = -1;
			}
		}
	}
	for (int fd : {in[1], outFd, errFd}) {
		if (fd >= 0) close(fd);
	}
	int wait = 0;
	waitpid(pid, &wait, 0);
	result.exitCode = WIFEXITED(wait) ? WEXITSTATUS(wait) : 1;
	return result;
}

#endif

}  // namespace kenzo
