#pragma once

#include <rtxui/rtxui.hpp>
#include <string>

namespace kenzo::ui {

class TextField {
public:
	bool handle(const rtxui::Event::Keyboard& key);

	void clear() {
		text_.clear();
		cursor_ = 0;
	}
	void set(std::string text) {
		text_ = std::move(text);
		cursor_ = text_.size();
	}

	[[nodiscard]] const std::string& text() const { return text_; }
	[[nodiscard]] bool empty() const { return text_.empty(); }

	[[nodiscard]] std::string before() const { return text_.substr(0, cursor_); }
	[[nodiscard]] std::string under() const;
	[[nodiscard]] std::string after() const;

private:
	[[nodiscard]] std::size_t next(std::size_t pos) const;
	[[nodiscard]] std::size_t prev(std::size_t pos) const;

	std::string text_;
	std::size_t cursor_ = 0;
};

}  // namespace kenzo::ui
