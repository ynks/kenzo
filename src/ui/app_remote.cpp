#include <algorithm>
#include <array>
#include <cctype>
#include <format>

#include "core/format.hpp"
#include "ui/app.hpp"

namespace kenzo::ui {
namespace {

using State = RemoteClient::State;

constexpr std::array<const char*, 10> kSpin = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};

std::string lowered(std::string text) {
	for (char& c : text) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return text;
}

// one bullet per character
std::string bullets(const std::string& text) {
	std::string out;
	for (const unsigned char c : text) {
		if ((c & 0xC0) != 0x80) {
			out += "\xE2\x80\xA2";
		}
	}
	return out;
}

std::string progressBar(std::uint64_t done, std::uint64_t total, int width) {
	const double fraction = total == 0 ? 0.0 : std::clamp(static_cast<double>(done) / static_cast<double>(total), 0.0, 1.0);
	const int filled = static_cast<int>(fraction * width);
	std::string bar;
	for (int i = 0; i < width; ++i) {
		bar += i < filled ? "█" : "░";
	}
	return bar;
}

}  // namespace

bool App::remoteSearchChanged() {
	remoteSel_ = 0;
	remoteTop_ = 0;
	return true;
}

void App::FocusRemoteSearch() {
	consumed_ = true;
	remoteSearchFocus_ = true;
}

void App::buildRemoteDialog() {
	const RemoteClient::Snapshot& snap = remote_->snapshot();
	rem_show_prompt = snap.state == State::NeedPassphrase;
	rem_show_list = snap.state == State::Ready;
	rem_show_status = false;
	remote_status_cls.clear();
	remote_status.clear();
	remote_summary = snap.state == State::Ready ? std::format("{} uploaded", snap.logs.size()) : std::string();

	switch (snap.state) {
		case State::Idle:
		case State::SigningIn:
			remote_status = std::string(kSpin[static_cast<std::size_t>(spinner_) % kSpin.size()]) + " signing in";
			rem_show_status = true;
			remote_hint = "esc close";
			break;
		case State::Loading:
			remote_status = std::string(kSpin[static_cast<std::size_t>(spinner_) % kSpin.size()]) + " loading sessions";
			rem_show_status = true;
			remote_hint = "esc close";
			break;
		case State::NeedPassphrase:
			remote_status = std::format("passphrase for {} ({} tries left)", snap.keyName, snap.triesLeft);
			remote_hint = "enter sign in · esc cancel";
			break;
		case State::Error:
			remote_status = snap.error + " · r retry";
			remote_status_cls = "bad";
			rem_show_status = true;
			remote_hint = "r retry · esc close";
			break;
		case State::Ready: remote_hint = "enter download · j/k move · / filter · r refresh · esc close"; break;
	}

	rem_pass_before = bullets(passphrase_.before());
	rem_pass_under = passphrase_.under().empty() ? std::string(" ") : "\xE2\x80\xA2";
	rem_pass_after = bullets(passphrase_.after());

	remoteList_.clear();
	const std::string needle = lowered(remoteSearch_.text());
	for (std::size_t i = 0; i < snap.logs.size(); ++i) {
		const RemoteLog& log = snap.logs[i];
		if (needle.empty() || lowered(log.host).find(needle) != std::string::npos || lowered(log.user).find(needle) != std::string::npos) {
			remoteList_.push_back(i);
		}
	}
	rem_empty = remoteList_.empty();

	const std::size_t visible = static_cast<std::size_t>(std::clamp(rowsTotal_ - 14, 3, 16));
	rem_list_h = static_cast<int>(std::max<std::size_t>(1, std::min(visible, std::max<std::size_t>(remoteList_.size(), 1))));
	if (remoteList_.empty()) {
		// room for the empty message
	}
	const std::size_t h = static_cast<std::size_t>(rem_list_h);
	if (!remoteList_.empty()) {
		remoteSel_ = std::min(remoteSel_, remoteList_.size() - 1);
	}
	if (remoteSel_ < remoteTop_) remoteTop_ = remoteSel_;
	if (remoteSel_ >= remoteTop_ + h) remoteTop_ = remoteSel_ + 1 - h;

	std::vector<RemoteRow> built;
	for (std::size_t i = remoteTop_; i < remoteList_.size() && i < remoteTop_ + h; ++i) {
		const RemoteLog& log = snap.logs[remoteList_[i]];
		RemoteRow row;
		row.cls = i == remoteSel_ ? "sel" : "";
		row.host = log.host;
		row.user = log.user;
		row.when = fmt::relativeTime(log.started);
		row.duration = fmt::duration(log.duration);
		row.size = fmt::fileSize(log.size);
		built.push_back(std::move(row));
	}
	rem_rows = std::move(built);

	if (snap.state == State::Ready) {
		if (remoteDownloading_) {
			remote_hint = "esc cancel download";
		} else if (!remoteMessage_.empty()) {
			remote_status = remoteMessage_;
			remote_status_cls = "bad";
			rem_show_status = true;
		}
	}

	dl_name = remoteName_;
	dl_bar = progressBar(remoteDone_, remoteTotal_, 36);
	if (remoteTotal_ == 0) {
		dl_detail = remoteDone_ == 0 ? "waiting for the server" : fmt::fileSize(remoteDone_);
	} else {
		dl_detail = std::format("{}%   {} / {}", static_cast<int>(std::min(100.0, 100.0 * static_cast<double>(remoteDone_) / static_cast<double>(remoteTotal_))),
		                        fmt::fileSize(remoteDone_), fmt::fileSize(remoteTotal_));
	}

	rem_search_cls = remoteSearchFocus_ ? "on" : "";
	rem_before = remoteSearchFocus_ ? remoteSearch_.before() : remoteSearch_.text();
	rem_under = remoteSearchFocus_ ? remoteSearch_.under() : "";
	rem_after = remoteSearchFocus_ ? remoteSearch_.after() : "";
	rem_cursor_cls = remoteSearchFocus_ ? "cursor" : "";
	rem_placeholder = remoteSearch_.empty() ? "filter by host or user" : "";
}

bool App::handlePassphraseKey(const Key& key) {
	using K = rtxui::Event::Keyboard;
	if (key.is(K::Return)) {
		remote_->submitPassphrase(passphrase_.text());
		passphrase_.clear();
	} else if (key.is(K::Escape)) {
		remote_->cancelPassphrase();
		passphrase_.clear();
		closeOverlay();
	} else {
		passphrase_.handle(*key.raw);
	}
	return true;
}

bool App::handleRemoteKey(const Key& key) {
	using K = rtxui::Event::Keyboard;
	const bool ready = remote_->snapshot().state == State::Ready;
	const std::size_t n = remoteList_.size();
	const auto move = [&](long d) {
		if (ready && n > 0) {
			remoteSel_ = static_cast<std::size_t>(std::clamp<long>(static_cast<long>(remoteSel_) + d, 0, static_cast<long>(n) - 1));
		}
	};
	if (key.is(K::Escape) || key.is('q')) {
		if (remoteDownloading_) {
			remote_->cancelDownload();
		} else {
			closeOverlay();
		}
	} else if (key.is('j') || key.is(K::ArrowDown)) {
		move(1);
	} else if (key.is('k') || key.is(K::ArrowUp)) {
		move(-1);
	} else if (key.isCtrl('d') || key.is(K::PageDown)) {
		move(static_cast<long>(rem_list_h) / 2 + 1);
	} else if (key.isCtrl('u') || key.is(K::PageUp)) {
		move(-(static_cast<long>(rem_list_h) / 2 + 1));
	} else if (key.is('g') || key.is(K::Home)) {
		remoteSel_ = 0;
	} else if (key.is('G') || key.is(K::End)) {
		move(1'000'000);
	} else if (key.is('/')) {
		remoteSearchFocus_ = ready;
	} else if (key.is('r')) {
		if (!remoteDownloading_) {
			remoteMessage_.clear();
			remote_->open();
		}
	} else if (key.is(K::Return) || key.is('l') || key.is(K::ArrowRight)) {
		if (ready && !remoteDownloading_ && remoteSel_ < n) {
			startRemoteDownload(remoteSel_);
		}
	} else {
		return false;
	}
	return true;
}

void App::startRemoteDownload(std::size_t listIndex) {
	const RemoteLog log = remote_->snapshot().logs[remoteList_[listIndex]];
	remoteDownloading_ = true;
	remoteName_ = log.host + "  " + log.user;
	remoteDone_ = 0;
	remoteTotal_ = 0;
	remoteMessage_.clear();
	remote_->download(
	    log,
	    [this](std::uint64_t done, std::uint64_t total) {
		    remoteDone_ = done;
		    remoteTotal_ = total;
	    },
	    [this](std::expected<std::filesystem::path, std::string> result) {
		    remoteDownloading_ = false;
		    if (result) {
			    closeOverlay();
			    startFile(*result);
		    } else if (result.error() != "cancelled") {
			    remoteMessage_ = result.error();
		    }
	    });
}

void App::RemoteRowClick(std::string index) {
	consumed_ = true;
	const std::size_t i = remoteTop_ + static_cast<std::size_t>(std::atoi(index.c_str()));
	if (i < remoteList_.size()) {
		remoteSel_ = i;
		if (!remoteDownloading_) {
			startRemoteDownload(i);
		}
	}
}

}  // namespace kenzo::ui
