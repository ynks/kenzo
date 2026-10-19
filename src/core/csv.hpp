#pragma once

#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "core/record.hpp"

namespace kenzo {

struct CsvEnds {
	std::optional<RawRecord> first, last;
};
CsvEnds readCsvEnds(const std::filesystem::path& path);

class CsvLoader {
public:
	struct Callbacks {
		std::function<void(std::vector<RawRecord>&&)> batch;
		std::function<void(double)> progress;
		std::function<void(std::string error)> done;
	};

	CsvLoader(std::filesystem::path path, Callbacks callbacks);
	~CsvLoader();
	CsvLoader(const CsvLoader&) = delete;
	CsvLoader& operator=(const CsvLoader&) = delete;

	void ack();

private:
	void run(std::stop_token stop);

	std::filesystem::path path_;
	Callbacks callbacks_;
	std::mutex mutex_;
	std::condition_variable_any cv_;
	int inflight_ = 0;
	std::jthread thread_;
};

}  // namespace kenzo
