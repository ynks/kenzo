#include "ui/textfield.hpp"

namespace kenzo::ui {
namespace {

void appendUtf8(std::string& out, char32_t cp) {
	if (cp < 0x80) {
		out.push_back(static_cast<char>(cp));
	} else if (cp < 0x800) {
		out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else if (cp < 0x10000) {
		out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else {
		out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
}

bool isWordChar(char c) {
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (c & 0x80);
}

}  // namespace

std::size_t TextField::next(std::size_t pos) const {
	if (pos >= text_.size()) {
		return text_.size();
	}
	++pos;
	while (pos < text_.size() && (static_cast<unsigned char>(text_[pos]) & 0xC0) == 0x80) {
		++pos;
	}
	return pos;
}

std::size_t TextField::prev(std::size_t pos) const {
	if (pos == 0) {
		return 0;
	}
	--pos;
	while (pos > 0 && (static_cast<unsigned char>(text_[pos]) & 0xC0) == 0x80) {
		--pos;
	}
	return pos;
}

std::string TextField::under() const {
	return cursor_ >= text_.size() ? " " : text_.substr(cursor_, next(cursor_) - cursor_);
}

std::string TextField::after() const {
	return cursor_ >= text_.size() ? std::string() : text_.substr(next(cursor_));
}

bool TextField::handle(const rtxui::Event::Keyboard& key) {
	using K = rtxui::Event::Keyboard;
	const auto mod = key.modifier();

	switch (key.special()) {
		case K::Backspace:
			if (cursor_ == 0) return false;
			text_.erase(prev(cursor_), cursor_ - prev(cursor_));
			cursor_ = prev(cursor_);
			return true;
		case K::Delete:
			if (cursor_ >= text_.size()) return false;
			text_.erase(cursor_, next(cursor_) - cursor_);
			return true;
		case K::ArrowLeft: cursor_ = prev(cursor_); return false;
		case K::ArrowRight: cursor_ = next(cursor_); return false;
		case K::Home: cursor_ = 0; return false;
		case K::End: cursor_ = text_.size(); return false;
		default: break;
	}

	const char32_t cp = key.codepoint();
	if (cp == 0) {
		return false;
	}
	if (mod.ctrl()) {
		switch (cp) {
			case 'a': cursor_ = 0; return false;
			case 'e': cursor_ = text_.size(); return false;
			case 'b': cursor_ = prev(cursor_); return false;
			case 'f': cursor_ = next(cursor_); return false;
			case 'u':
				text_.erase(0, cursor_);
				cursor_ = 0;
				return true;
			case 'k':
				text_.erase(cursor_);
				return true;
			case 'w': {
				std::size_t p = cursor_;
				while (p > 0 && !isWordChar(text_[p - 1])) --p;
				while (p > 0 && isWordChar(text_[p - 1])) --p;
				text_.erase(p, cursor_ - p);
				cursor_ = p;
				return true;
			}
			default: return false;
		}
	}
	if (mod.alt() || mod.meta() || cp < 0x20 || cp == 0x7f) {
		return false;
	}
	std::string encoded;
	appendUtf8(encoded, cp);
	text_.insert(cursor_, encoded);
	cursor_ += encoded.size();
	return true;
}

}  // namespace kenzo::ui
