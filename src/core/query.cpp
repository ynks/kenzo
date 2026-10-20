#include "core/query.hpp"

#include <algorithm>
#include <cctype>

#include "core/format.hpp"

namespace kenzo {
namespace {

char lower(char c) {
	return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

std::string toLower(std::string_view s) {
	std::string out(s);
	for (char& c : out) {
		c = lower(c);
	}
	return out;
}

}  // namespace

std::size_t ifind(std::string_view hay, std::string_view needle, std::size_t from) {
	if (needle.empty()) {
		return from <= hay.size() ? from : std::string_view::npos;
	}
	if (hay.size() < needle.size()) {
		return std::string_view::npos;
	}
	const char c0 = needle[0];
	const char u0 = static_cast<char>(std::toupper(static_cast<unsigned char>(c0)));
	const std::size_t last = hay.size() - needle.size();
	for (std::size_t i = from; i <= last; ++i) {
		const char h = hay[i];
		if (h != c0 && h != u0) {
			continue;
		}
		std::size_t j = 1;
		while (j < needle.size() && lower(hay[i + j]) == needle[j]) {
			++j;
		}
		if (j == needle.size()) {
			return i;
		}
	}
	return std::string_view::npos;
}

bool icontains(std::string_view hay, std::string_view needle) {
	return ifind(hay, needle) != std::string_view::npos;
}

std::string_view severityName(int sev) {
	static constexpr std::string_view names[] = {"TRACE", "INFO", "WARNING", "ERROR", "CRITICAL"};
	return sev >= 0 && sev < kSevCount ? names[sev] : "?";
}

std::string_view severityShort(int sev) {
	static constexpr std::string_view names[] = {"TRC", "INF", "WRN", "ERR", "CRT"};
	return sev >= 0 && sev < kSevCount ? names[sev] : "???";
}

Query Query::parse(std::string_view text) {
	Query q;
	std::size_t i = 0;
	const auto isSpace = [](char c) { return c == ' ' || c == '\t'; };

	while (i < text.size()) {
		while (i < text.size() && isSpace(text[i])) {
			++i;
		}
		if (i >= text.size()) {
			break;
		}

		Term term;
		if (text[i] == '-' && i + 1 < text.size() && !isSpace(text[i + 1])) {
			term.negate = true;
			++i;
		}

		static constexpr std::pair<std::string_view, Field> prefixes[] = {
		    {"sink:", Field::Sink}, {"file:", Field::File}, {"msg:", Field::Message}, {"sev:", Field::Severity}, {"bt:", Field::Backtrace}, {"time:", Field::Time},
		};
		for (const auto& [prefix, field] : prefixes) {
			if (text.size() - i >= prefix.size() && ifind(text.substr(i, prefix.size()), prefix) == 0) {
				term.field = field;
				i += prefix.size();
				break;
			}
		}

		std::string_view word;
		if (i < text.size() && text[i] == '"') {
			const std::size_t close = text.find('"', i + 1);
			const std::size_t end = close == std::string_view::npos ? text.size() : close;
			word = text.substr(i + 1, end - i - 1);
			i = close == std::string_view::npos ? text.size() : close + 1;
		} else {
			const std::size_t start = i;
			while (i < text.size() && !isSpace(text[i])) {
				++i;
			}
			word = text.substr(start, i - start);
		}

		if (!word.empty()) {
			term.needle = toLower(word);
			term.timeLike = std::ranges::all_of(term.needle, [](char c) { return (c >= '0' && c <= '9') || c == ':' || c == '.' || c == '-'; });
			q.terms_.push_back(std::move(term));
		}
	}
	return q;
}

bool Query::matches(const Data& data, const LogRecord& rec) const {
	for (const Term& t : terms_) {
		bool hit = false;
		switch (t.field) {
			case Field::Any:
				hit = icontains(rec.message, t.needle) || icontains(data.sinks().get(rec.sink), t.needle) ||
				      icontains(data.files().get(rec.file), t.needle) || icontains(severityName(rec.sev), t.needle) ||
				      icontains(rec.details, t.needle);
				if (!hit && t.timeLike) {
					hit = icontains(fmt::clockTime(rec.ts), t.needle);
				}
				break;
			case Field::Sink: hit = icontains(data.sinks().get(rec.sink), t.needle); break;
			case Field::File: hit = icontains(data.files().get(rec.file), t.needle); break;
			case Field::Message: hit = icontains(rec.message, t.needle) || icontains(rec.details, t.needle); break;
			case Field::Severity: hit = icontains(severityName(rec.sev), t.needle); break;
			case Field::Backtrace: hit = rec.backtrace != 0 && icontains(data.backtraces().get(rec.backtrace), t.needle); break;
			case Field::Time: hit = icontains(fmt::clockTime(rec.ts), t.needle); break;
		}
		if (hit == t.negate) {
			return false;
		}
	}
	return true;
}

std::vector<std::string_view> Query::highlights(Field column) const {
	std::vector<std::string_view> out;
	for (const Term& t : terms_) {
		if (!t.negate && (t.field == column || t.field == Field::Any)) {
			out.emplace_back(t.needle);
		}
	}
	return out;
}

}  // namespace kenzo
