#include "core/csv.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <vincentlaucsb-csv-parser/csv.hpp>

namespace kenzo {
namespace {

constexpr std::size_t kBatchRows = 20'000;
constexpr int kMaxInflight = 3;
constexpr std::size_t kTailBytes = 256 * 1024;

csv::CSVFormat sessionFormat() {
	csv::CSVFormat format;
	format.delimiter(',').quote('"').no_header().variable_columns(csv::VariableColumnPolicy::KEEP);
	return format;
}

int severityFromText(std::string_view s) {
	static constexpr std::string_view names[] = {"TRACE", "INFO", "WARNING", "ERROR", "CRITICAL"};
	for (int i = 0; i < kSevCount; ++i) {
		const auto name = names[i];
		if (s.size() == name.size() &&
		    std::ranges::equal(s, name, [](char a, char b) { return std::toupper(static_cast<unsigned char>(a)) == b; })) {
			return i;
		}
	}
	return s == "WARN" ? kWarning : -1;
}

template <typename T>
bool parseNumber(std::string_view s, T& out) {
	const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
	return ec == std::errc() && ptr == s.data() + s.size();
}

std::string_view field(const csv::CSVRow& row, std::size_t i) {
	const auto sv = row[i].get_sv();
	return {sv.data(), sv.size()};
}

bool recordFromRow(const csv::CSVRow& row, RawRecord& out) {
	if (row.size() < 5) {
		return false;
	}
	std::uint64_t ts = 0;
	if (!parseNumber(field(row, 0), ts)) {
		return false;
	}
	const int sev = severityFromText(field(row, 1));
	out = {};
	out.ts = ts < 100'000'000'000'000ull ? ts * 1'000'000ull : ts;  // old servers wrote ms
	out.sev = static_cast<std::uint8_t>(sev < 0 ? kInfo : sev);
	out.file = field(row, 2);
	parseNumber(field(row, 3), out.line);
	out.sink = field(row, 4);
	if (row.size() > 5) out.message = field(row, 5);
	if (row.size() > 6) out.details = field(row, 6);
	if (row.size() > 7) out.backtrace = field(row, 7);
	return true;
}

std::optional<RawRecord> firstRecord(std::string_view text) {
	auto reader = csv::parse(csv::string_view(text.data(), text.size()), sessionFormat());
	RawRecord rec;
	for (const csv::CSVRow& row : reader) {
		if (recordFromRow(row, rec)) {
			return rec;
		}
	}
	return std::nullopt;
}

std::optional<RawRecord> lastRecord(std::string_view text) {
	for (std::size_t i = text.size(); i-- > 0;) {
		if (i != 0 && text[i - 1] != '\n') {
			continue;
		}
		std::size_t p = i;
		while (p < text.size() && text[p] >= '0' && text[p] <= '9') ++p;
		if (p == i || p >= text.size() || text[p] != ',') continue;
		const std::size_t end = text.find(',', p + 1);
		if (end == std::string_view::npos || severityFromText(text.substr(p + 1, end - p - 1)) < 0) continue;
		std::optional<RawRecord> rec;
		auto reader = csv::parse(csv::string_view(text.data() + i, text.size() - i), sessionFormat());
		RawRecord candidate;
		for (const csv::CSVRow& row : reader) {
			if (recordFromRow(row, candidate)) {
				rec = std::move(candidate);
				break;
			}
		}
		return rec;
	}
	return std::nullopt;
}

}  // namespace

CsvEnds readCsvEnds(const std::filesystem::path& path) {
	CsvEnds ends;
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		return ends;
	}
	std::string head(kTailBytes, '\0');
	in.read(head.data(), static_cast<std::streamsize>(head.size()));
	head.resize(static_cast<std::size_t>(in.gcount()));
	ends.first = firstRecord(head);

	std::error_code ec;
	const auto size = std::filesystem::file_size(path, ec);
	if (ec) {
		return ends;
	}
	std::string tail;
	if (size <= kTailBytes) {
		tail = head;
	} else {
		in.clear();
		in.seekg(static_cast<std::streamoff>(size - kTailBytes));
		tail.resize(kTailBytes);
		in.read(tail.data(), static_cast<std::streamsize>(tail.size()));
		tail.resize(static_cast<std::size_t>(in.gcount()));
	}
	ends.last = lastRecord(tail);
	return ends;
}

CsvLoader::CsvLoader(std::filesystem::path path, Callbacks callbacks)
    : path_(std::move(path)), callbacks_(std::move(callbacks)), thread_([this](std::stop_token stop) { run(stop); }) {}

CsvLoader::~CsvLoader() {
	thread_.request_stop();
	cv_.notify_all();
}

void CsvLoader::ack() {
	{
		std::lock_guard lock(mutex_);
		--inflight_;
	}
	cv_.notify_all();
}

void CsvLoader::run(std::stop_token stop) {
	std::error_code ec;
	const auto total = static_cast<double>(std::max<std::uintmax_t>(1, std::filesystem::file_size(path_, ec)));
	if (ec) {
		callbacks_.done("Cannot open " + path_.string());
		return;
	}

	try {
		csv::CSVReader reader(path_.string(), sessionFormat());
		std::vector<RawRecord> batch;
		batch.reserve(kBatchRows);
		double consumed = 0;

		const auto flush = [&]() {
			if (batch.empty()) {
				return true;
			}
			{
				std::unique_lock lock(mutex_);
				if (!cv_.wait(lock, stop, [&] { return inflight_ < kMaxInflight; })) {
					return false;
				}
				++inflight_;
			}
			callbacks_.batch(std::move(batch));
			batch = {};
			batch.reserve(kBatchRows);
			callbacks_.progress(std::min(1.0, consumed / total));
			return true;
		};

		RawRecord rec;
		for (const csv::CSVRow& row : reader) {
			if (stop.stop_requested()) {
				return;
			}
			if (recordFromRow(row, rec)) {
				consumed += static_cast<double>(rec.message.size() + rec.details.size() + rec.backtrace.size() +
				                                rec.file.size() + rec.sink.size() + 40);
				batch.push_back(std::move(rec));
				if (batch.size() >= kBatchRows && !flush()) {
					return;
				}
			}
		}
		if (!flush()) {
			return;
		}
		callbacks_.done({});
	} catch (const std::exception& e) {
		callbacks_.done(e.what());
	}
}

}  // namespace kenzo
