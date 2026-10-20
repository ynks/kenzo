#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/store.hpp"

namespace kenzo {

bool icontains(std::string_view haystack, std::string_view lowerNeedle);

std::size_t ifind(std::string_view haystack, std::string_view lowerNeedle, std::size_t from = 0);

std::string_view severityName(int sev);
std::string_view severityShort(int sev);

class Query {
public:
	enum class Field { Any, Sink, File, Message, Severity, Backtrace, Time };

	struct Term {
		Field field = Field::Any;
		std::string needle;
		bool negate = false;
		bool timeLike = false;
	};

	static Query parse(std::string_view text);

	[[nodiscard]] bool empty() const { return terms_.empty(); }
	[[nodiscard]] bool matches(const Data& data, const LogRecord& rec) const;
	[[nodiscard]] const std::vector<Term>& terms() const { return terms_; }

	[[nodiscard]] std::vector<std::string_view> highlights(Field column) const;

private:
	std::vector<Term> terms_;
};

}  // namespace kenzo
