#include "ui/app.hpp"

#include <algorithm>
#include <format>

#include "core/clipboard.hpp"
#include "ui/terminal.hpp"

namespace kenzo::ui {
namespace {

void post(std::function<void()> task) {
	rtxui::PostTask(std::move(task));
}

constexpr std::array<const char*, 10> kSpinner = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
constexpr auto kTick = std::chrono::milliseconds(66);

}  // namespace

std::string_view App::viewText() {
	static const std::string text = loadView();
	return text;
}

App::App(Options options) : options_(std::move(options)) {
	Bind(app_cls);
	Bind(show_menu);
	Bind(grid_rows);
	Bind(grid_accent);
	Bind(grid_opacity);
	Bind(menu_cls0);
	Bind(menu_cls1);
	Bind(menu_cls_more);
	Bind(menu_icon0);
	Bind(menu_icon1);
	Bind(menu_note0);
	Bind(menu_note1);
	Bind(menu_note_cls0);
	Bind(menu_note_cls1);
	Bind(lan_editing);
	Bind(lan_idle);
	Bind(lan_before);
	Bind(lan_under);
	Bind(lan_after);
	Bind(menu_rule);
	Bind(menu_more_count);
	Bind(menu_error);
	Bind(menu_hint);
	Bind(menu_sessions);
	Bind(menu_no_sessions);
	Bind(show_main);
	Bind(main_cls);
	Bind(time_header);
	Bind(sink_w);
	Bind(file_w);
	Bind(rows);
	Bind(rows_empty);
	Bind(empty_text);
	Bind(bar_cells);
	Bind(search_cls);
	Bind(search_icon);
	Bind(search_before);
	Bind(search_cursor_cls);
	Bind(search_under);
	Bind(search_after);
	Bind(search_placeholder);
	Bind(search_info);
	Bind(search_info_cls);
	Bind(st_mode_cls);
	Bind(st_mode);
	Bind(st_conn_cls);
	Bind(st_conn);
	Bind(st_pending);
	Bind(follow_cls);
	Bind(follow_text);
	Bind(cnt_cls0);
	Bind(cnt_cls1);
	Bind(cnt_cls2);
	Bind(cnt_cls3);
	Bind(cnt0);
	Bind(cnt1);
	Bind(cnt2);
	Bind(cnt3);
	Bind(st_hints_cls);
	Bind(st_show_hints);
	Bind(st_show_follow);
	Bind(st_show_levels);
	Bind(st_hint_x);
	Bind(cnt_label0);
	Bind(cnt_label1);
	Bind(cnt_label2);
	Bind(cnt_label3);
	Bind(show_toast);
	Bind(toast_cls);
	Bind(toast_text);
	Bind(show_sev);
	Bind(show_sinks);
	Bind(show_sessions);
	Bind(show_help);
	Bind(show_ctx);
	Bind(show_ctx_sub);
	Bind(show_detail);
	Bind(sev_rows);
	Bind(sinks_summary);
	Bind(sinks_search_cls);
	Bind(sinks_before);
	Bind(sinks_cursor_cls);
	Bind(sinks_under);
	Bind(sinks_after);
	Bind(sinks_placeholder);
	Bind(sinks_list_h);
	Bind(sink_rows);
	Bind(sinks_empty);
	Bind(sessions_summary);
	Bind(sess_search_cls);
	Bind(sess_before);
	Bind(sess_cursor_cls);
	Bind(sess_under);
	Bind(sess_after);
	Bind(sess_placeholder);
	Bind(sess_list_h);
	Bind(sess_rows);
	Bind(sess_empty);
	Bind(help_h);
	Bind(help_rows);
	Bind(ctx_x);
	Bind(ctx_y);
	Bind(ctx_sub_x);
	Bind(ctx_sub_y);
	Bind(ctx_items);
	Bind(ctx_sub_items);
	Bind(dv_bar_cls);
	Bind(dv_sev_cls);
	Bind(dv_sev);
	Bind(dv_sink);
	Bind(dv_pos);
	Bind(dv_time);
	Bind(dv_file);
	Bind(dv_layout);
	Bind(dv_msg_focus);
	Bind(dv_bt_focus);
	Bind(dv_rule);
	Bind(dv_bt_count);
	Bind(dv_bt_btn_cls);
	Bind(dv_msg_lines);
	Bind(dv_det_lines);
	Bind(dv_bt_lines);
	Bind(dv_has_details);
	Bind(dv_has_bt);
	Bind(dv_msg_bar);
	Bind(dv_bt_bar);
	Bind(dv_w);
	Bind(dv_h);
	Bind(dv_lw);
	Bind(dv_lh);
	Bind(dv_rw);
	Bind(dv_rh);
	Bind(MenuPick);
	Bind(MenuSession);
	Bind(RowClick);
	Bind(RowContext);
	Bind(BarJump);
	Bind(PaneBarMsg);
	Bind(PaneBarBt);
	Bind(FocusSearch);
	Bind(ToggleGroup);
	Bind(ToggleFollow);
	Bind(ToggleTime);
	Bind(OpenSinks);
	Bind(OpenLevels);
	Bind(OpenHelp);
	Bind(ClearClick);
	Bind(BackdropClick);
	Bind(Inside);
	Bind(CloseOverlay);
	Bind(SevRowClick);
	Bind(SinkRowClick);
	Bind(SinkToggle);
	Bind(SinkChip);
	Bind(FocusSinkSearch);
	Bind(FocusSessSearch);
	Bind(SessRowClick);
	Bind(CtxPick);
	Bind(CtxSubPick);
	Bind(FocusPane);
	Bind(DetailCopy);
	Bind(DetailCopyMessage);
	Bind(DetailCopyBacktrace);

	index_ = std::make_unique<ViewIndex>(post);
	index_->reset(store_.data());

	connection_ = std::make_unique<Connection>(post, Connection::Handlers {
	                                                      [this](Connection::State s) { onLinkState(s); },
	                                                      [this](std::vector<RawRecord>&& b) { onBatch(std::move(b)); },
	                                                  });
	scanner_ = std::make_unique<SessionScanner>(post);

	buildHelp();
	randomizeBackground();
	resetMenuIdle();
	if (!options_.csv && !options_.host) beginBackgroundEntrance(true);
	refreshSize();
	buildGrid();
}

App::~App() {
	stopSource();
}

void App::stopSource() {
	++sourceGen_;
	connection_->stop();
	loader_.reset();
}

void App::startLocalhost() {
	stopSource();
	source_ = Source::Localhost;
	target_ = Target {};
	link_ = Link::Searching;
	everConnected_ = false;
	connection_->connect(target_);
}

void App::startLan(const Target& target) {
	stopSource();
	source_ = Source::Lan;
	target_ = target;
	link_ = Link::Searching;
	everConnected_ = false;
	connection_->connect(target_);
}

void App::startFile(const std::filesystem::path& path) {
	stopSource();
	source_ = Source::File;
	fileName_ = path.filename().string();
	link_ = Link::Connected;
	loadProgress_ = 0;
	resetStore(false);
	const auto gen = sourceGen_;

	CsvLoader::Callbacks callbacks;
	callbacks.batch = [this, gen](std::vector<RawRecord>&& batch) {
		post([this, gen, shared = std::make_shared<std::vector<RawRecord>>(std::move(batch))] {
			if (gen != sourceGen_ || !loader_) {
				return;
			}
			onBatch(std::move(*shared));
			loader_->ack();
		});
	};
	callbacks.progress = [this, gen](double p) {
		post([this, gen, p] {
			if (gen == sourceGen_) {
				loadProgress_ = p;
			}
		});
	};
	callbacks.done = [this, gen](std::string error) {
		post([this, gen, error = std::move(error)] {
			if (gen != sourceGen_) {
				return;
			}
			loadProgress_ = 1.0;
			if (!error.empty()) {
				toast(error, "bad");
			}
		});
	};
	loader_ = std::make_unique<CsvLoader>(path, std::move(callbacks));
	enterMain();
}

void App::enterMain() {
	show_menu = false;
	show_main = true;
	lanFocus_ = false;
	closeOverlay();
}

void App::leaveToMenu() {
	stopSource();
	source_ = Source::None;
	link_ = Link::Searching;
	menuAuto_ = false;
	resetStore(false);
	closeOverlay();
	searchFocus_ = false;
	search_.clear();
	lastQueryText_.clear();
	index_->setQuery(Query {});
	index_->setFilter(FilterState {});
	undoStack_.clear();
	undoLabels_.clear();
	show_main = false;
	show_menu = true;
	resetMenuIdle();
	randomizeBackground();
	beginBackgroundEntrance();
	buildGrid();
	menuSel_ = 0;
	menu_error.clear();
	refreshSessions();
}

void App::onLinkState(Connection::State state) {
	switch (state) {
		case Connection::State::Searching:
			link_ = Link::Searching;
			break;
		case Connection::State::Connected:
			link_ = Link::Connected;
			everConnected_ = true;
			resetStore(false);  // replay incoming
			follow_ = true;
			if (show_menu && !lanFocus_ && overlay_ != Overlay::Sessions) {
				enterMain();
			}
			break;
		case Connection::State::Disconnected:
			link_ = Link::Disconnected;
			break;
	}
}

void App::onBatch(std::vector<RawRecord>&& batch) {
	for (RawRecord& raw : batch) {
		if (store_.size() == 0 && firstTs_ == 0) {
			firstTs_ = raw.ts;
		}
		if (!store_.append(std::move(raw))) {
			toast("Log store is full", "bad");
			break;
		}
	}
	index_->sync();
}

void App::resetStore(bool announce) {
	store_.clear();
	store_.dropUndo();
	clearedRecently_ = false;
	index_->reset(store_.data());
	sel_ = 0;
	scrollTop_ = 0;
	follow_ = true;
	firstTs_ = 0;
	haveSelRecord_ = false;
	if (announce) {
		toast("Cleared", "good");
	}
}

void App::refreshSize() {
	const auto [w, h] = terminalSize();
	const bool changed = cols_ != w || rowsTotal_ != h;
	cols_ = w;
	rowsTotal_ = h;
	if (changed && show_menu) buildGrid();
}

void App::refreshFlags() {
	show_sev = overlay_ == Overlay::Severities;
	show_sinks = overlay_ == Overlay::Sinks;
	show_sessions = overlay_ == Overlay::Sessions;
	show_help = overlay_ == Overlay::Help;
	show_ctx = overlay_ == Overlay::Context;
	show_detail = overlay_ == Overlay::Detail;
	if (!show_ctx) {
		show_ctx_sub = false;
	}
	app_cls = grid_accent + (options_.reducedMotion ? " reduced" : "");
	main_cls = std::string(cols_ < 120 ? "compact" : "") + (cols_ < 76 ? " tiny" : "");
	show_toast = Clock::now() < toastUntil_;
}

bool App::Digest() {
	if (!tickStarted_) {
		tickStarted_ = true;  // timers need the loop running
		scheduleTick();
		refreshSessions();
		if (options_.csv) {
			startFile(*options_.csv);
		} else if (options_.host) {
			startLan(*options_.host);
		} else {
			startLocalhost();
		}
	}
	refreshSize();
	index_->sync();
	refreshFlags();
	if (show_menu) {
		buildMenu();
		if (overlay_ == Overlay::Sessions) {
			buildSessionsDialog();
		}
	}
	if (show_main) {
		followIfNeeded();
		buildRows();
		buildBar();
		buildStatus();
		buildSearch();
		switch (overlay_) {
			case Overlay::Severities: buildSeverities(); break;
			case Overlay::Sinks: buildSinks(); break;
			case Overlay::Help: buildHelp(); break;
			case Overlay::Context: buildContext(); break;
			case Overlay::Detail: buildDetail(); break;
			default: break;
		}
	} else if (overlay_ == Overlay::Help) {
		buildHelp();
	}
	return Component<App>::Digest();
}

void App::scheduleTick() {
	rtxui::PostDelayedTask([this] { tick(); }, kTick);
}

void App::tick() {
	++frame_;
	const bool busy = link_ != Link::Connected || index_->rebuilding() || loadProgress_ < 1.0;
	if (busy) {
		spinner_ = static_cast<int>(frame_ % kSpinner.size());
	}
	if (show_menu && !options_.reducedMotion) {
		updateMenuIdle();
		buildGrid();
	}
	if (chord_ != 0 && Clock::now() - chordAt_ > std::chrono::milliseconds(900)) {
		chord_ = 0;
		count_ = 0;
	}
	if (clearedRecently_ && Clock::now() - clearedAt_ > std::chrono::seconds(8)) {
		clearedRecently_ = false;
		store_.dropUndo();
	}
	if (show_menu && Clock::now() - lastSessionScan_ > std::chrono::seconds(5)) {
		refreshSessions();
	}
	scheduleTick();
}

void App::toast(std::string text, std::string kind) {
	toast_text = std::move(text);
	toast_cls = std::move(kind);
	toastUntil_ = Clock::now() + std::chrono::milliseconds(2600);
	show_toast = true;
}

void App::copyText(const std::string& text, const char* what) {
	if (options_.clipboard) {
		options_.clipboard(text);
	} else if (!copyToSystemClipboard(text)) {
		SetClipboard(text);
	}
	toast(std::format("Copied {}", what), "good");
}

bool App::OnEvent(rtxui::Event event) {
	if (show_menu && (event.is<rtxui::Event::Mouse>() || event.is<rtxui::Event::Keyboard>())) {
		menuActivityAt_ = menuNow();
	}
	if (auto mouse = event.get_if<rtxui::Event::Mouse>()) {
		handleMouse(*mouse);
		consumed_ = false;
		contextRequested_ = false;
		return false;
	}
	if (auto kb = event.get_if<rtxui::Event::Keyboard>()) {
		if (kb->motion() == rtxui::Event::Keyboard::Released) {
			return Component<App>::OnEvent(event);
		}
		Key key;
		key.raw = &*kb;
		key.special = kb->special();
		key.ctrl = kb->modifier().ctrl();
		key.alt = kb->modifier().alt();
		key.shift = kb->modifier().shift();
		key.ch = kb->codepoint();
		if (key.special == rtxui::Event::Keyboard::None && !key.ctrl && key.ch > 0 && key.ch < 27) {
			key.ctrl = true;
			key.ch += 96;
		}
		if (key.special == rtxui::Event::Keyboard::None && key.ch == 0 && !key.ctrl && !key.alt && overlay_ == Overlay::Sinks) {
			key.ch = ' ';  // nul is ctrl-space
			key.ctrl = true;
		}
		if (key.isCtrl('c')) {
			return Component<App>::OnEvent(event);
		}
		const bool handled = handleKey(key);
		return handled || key.is(rtxui::Event::Keyboard::Escape);
	}
	return Component<App>::OnEvent(event);
}

void App::handleMouse(const rtxui::Event::Mouse& mouse) {
	using Button = rtxui::Event::Mouse;
	mouseX_ = mouse.x();
	mouseY_ = mouse.y();

	if (overlay_ == Overlay::Context) {
		const int x = mouseX_ - 1, y = mouseY_ - 1;
		if (show_ctx_sub && x >= ctx_sub_x && x < ctx_sub_x + 34 && y > ctx_sub_y && y <= ctx_sub_y + 3) {
			ctxInSub_ = true;
			ctxSubSel_ = static_cast<std::size_t>(y - ctx_sub_y - 1);
		} else if (x >= ctx_x && x < ctx_x + 28 && y > ctx_y && y <= ctx_y + 3) {
			const auto next = static_cast<std::size_t>(y - ctx_y - 1);
			if (next != ctxSel_ || next == 0) {
				ctxInSub_ = false;
			}
			ctxSel_ = next;
		}
	}

	switch (mouse.button()) {
		case Button::WheelUp:
		case Button::WheelDown: {
			const long dir = mouse.button() == Button::WheelUp ? -1 : 1;
			switch (overlay_) {
				case Overlay::None:
					if (show_main) {
						scrollBy(dir * 3);
					} else if (show_menu) {
						menuSel_ = std::clamp(menuSel_ + static_cast<int>(dir), 0, 5);
					}
					break;
				case Overlay::Sinks:
					sinkSel_ = static_cast<std::size_t>(std::clamp<long>(static_cast<long>(sinkSel_) + dir * 3, 0,
					                                                       std::max<long>(0, static_cast<long>(sinkList_.size()) - 1)));
					break;
				case Overlay::Sessions:
					sessSel_ = static_cast<std::size_t>(std::clamp<long>(static_cast<long>(sessSel_) + dir * 3, 0,
					                                                       std::max<long>(0, static_cast<long>(sessList_.size()) - 1)));
					break;
				case Overlay::Help: helpTop_ = static_cast<std::size_t>(std::max<long>(0, static_cast<long>(helpTop_) + dir * 3)); break;
				case Overlay::Detail: {
					if (const int pane = paneAt(mouseX_ - 1, mouseY_ - 1); pane >= 0) {
						detailPane_ = pane;
					}
					scrollPane(detailPane_, dir * 3);
					break;
				}
				default: break;
			}
			return;
		}
		case Button::Right:
			if (contextRequested_ && mouse.motion() == Button::Released) {
				openContext(contextPosition_, mouseX_ - 1, mouseY_ - 1);
			}
			return;
		case Button::Left:
			if (mouse.motion() != Button::Released) {
				return;
			}
			if (!consumed_ && overlay_ == Overlay::Context) {
				closeOverlay();
			}
			return;
		default: return;
	}
}

bool App::handleTextKey(TextField& field, const Key& key, bool& focus, bool (App::*onChange)()) {
	if (key.is(rtxui::Event::Keyboard::Escape) || key.is(rtxui::Event::Keyboard::Return)) {
		focus = false;
		return true;
	}
	if (field.handle(*key.raw) && onChange != nullptr) {
		(this->*onChange)();
	}
	return true;
}

bool App::handleKey(const Key& key) {
	if (lanFocus_) {
		if (key.is(rtxui::Event::Keyboard::Return)) {
			Target t;
			if (parseTarget(lan_.text(), t)) {
				menu_error.clear();
				lanFocus_ = false;
				menuAuto_ = true;
				startLan(t);
			} else {
				menu_error = "Enter an address like 192.168.1.20 or host:12801";
			}
			return true;
		}
		if (key.is(rtxui::Event::Keyboard::Escape)) {
			lanFocus_ = false;
			menu_error.clear();
			if (source_ != Source::Localhost) {
				menuAuto_ = true;
				startLocalhost();
			}
			return true;
		}
		return handleTextKey(lan_, key, lanFocus_, &App::lanChanged);
	}
	if (searchFocus_) {
		if (key.is(rtxui::Event::Keyboard::Escape) && !search_.empty()) {
			searchFocus_ = false;
			return true;
		}
		return handleTextKey(search_, key, searchFocus_, &App::searchChanged);
	}
	if (sinkSearchFocus_) {
		return handleTextKey(sinkSearch_, key, sinkSearchFocus_, &App::sinkSearchChanged);
	}
	if (sessSearchFocus_) {
		return handleTextKey(sessSearch_, key, sessSearchFocus_, &App::sessSearchChanged);
	}

	switch (overlay_) {
		case Overlay::Detail: return handleDetailKey(key);
		case Overlay::Context: return handleContextKey(key);
		case Overlay::Severities: return handleSeverityKey(key);
		case Overlay::Sinks: return handleSinkKey(key);
		case Overlay::Sessions: return handleSessionsKey(key);
		case Overlay::Help: return handleHelpKey(key);
		case Overlay::None: break;
	}
	return show_menu ? handleMenuKey(key) : handleTableKey(key);
}

void App::Inside() {
	consumed_ = true;
}

void App::BackdropClick() {
	if (!consumed_) {
		closeOverlay();
	}
}

void App::CloseOverlay() {
	consumed_ = true;
	closeOverlay();
}

void App::ToggleFollow() {
	consumed_ = true;
	follow_ = !follow_;
	if (follow_) {
		goBottom();
	}
}

}  // namespace kenzo::ui

namespace kenzo::ui {

void App::ToggleTime() {
	consumed_ = true;
	relativeTime_ = !relativeTime_;
}

void App::OpenSinks() {
	consumed_ = true;
	if (overlay_ == Overlay::None) openOverlay(Overlay::Sinks);
}

void App::OpenLevels() {
	consumed_ = true;
	if (overlay_ == Overlay::None) openOverlay(Overlay::Severities);
}

void App::OpenHelp() {
	consumed_ = true;
	if (overlay_ == Overlay::None) openOverlay(Overlay::Help);
}

void App::ClearClick() {
	consumed_ = true;
	if (overlay_ == Overlay::None) clearAll();
}

}  // namespace kenzo::ui
