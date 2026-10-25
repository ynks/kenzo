#pragma once

#include <chrono>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace kenzo {

struct ProcessResult {
	bool started = false;
	bool timedOut = false;
	int exitCode = -1;
	std::string out;
	std::string err;
};

ProcessResult runProcess(const std::vector<std::string>& argv, std::string_view stdinData, const std::map<std::string, std::string>& env,
                         std::chrono::milliseconds timeout);

}  // namespace kenzo
