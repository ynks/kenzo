#pragma once

#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <rtxui/rtxui.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "core/csv.hpp"
#include "core/filter.hpp"
#include "core/sessions.hpp"
#include "core/store.hpp"
#include "net/connection.hpp"
#include "ui/assets.hpp"
#include "ui/textfield.hpp"
#include "ui/views.hpp"

namespace kenzo::ui {

struct Options {
	std::optional<std::filesystem::path> csv;
	std::optional<Target> host;
	bool reducedMotion = false;
	std::function<void(std::string_view)> clipboard;
	std::function<std::chrono::steady_clock::time_point()> menuClock;
};

enum class Source { None, Localhost, Lan, File };
enum class Link { Searching, Connected, Disconnected };
enum class Overlay { None, Severities, Sinks, Sessions, Help, Context, Detail };

using Clock = std::chrono::steady_clock;

class App : public rtxui::Component<App> {
public:
	explicit App(Options options);
	~App() override;

	bool OnEvent(rtxui::Event event) override;
	bool Digest() override;

	std::string_view view = viewText();

	void setQuit(std::function<void()> quit) { quit_ = std::move(quit); }

	std::string app_cls;

	bool show_menu = true;
	std::vector<GridRun> grid_rows;
	std::string grid_accent;
	std::string grid_opacity = "1";
	std::string menu_cls0, menu_cls1, menu_cls_more, menu_icon0, menu_icon1, menu_note0, menu_note1, menu_note_cls0, menu_note_cls1;
	bool lan_editing = false, lan_idle = true;
	std::string lan_before, lan_under, lan_after;
	std::string menu_rule, menu_more_count, menu_error, menu_hint;
	std::vector<MenuSessionRow> menu_sessions;
	bool menu_no_sessions = false;

	bool show_main = false;
	std::string main_cls, time_header = "Time";
	int sink_w = 12, file_w = 24;
	std::vector<LogRow> rows;
	bool rows_empty = false;
	std::string empty_text;
	std::vector<BarCell> bar_cells;
	std::string search_cls, search_icon = "/", search_before, search_cursor_cls, search_under, search_after, search_placeholder,
	                                           search_info, search_info_cls;
	std::string st_mode_cls, st_mode, st_conn_cls, st_conn, st_pending, follow_cls, follow_text;
	std::string cnt_cls0, cnt_cls1, cnt_cls2, cnt_cls3, cnt0, cnt1, cnt2, cnt3;
	std::string st_hints_cls, cnt_label0, cnt_label1, cnt_label2, cnt_label3;
	bool st_show_hints = true, st_show_follow = true, st_show_levels = true;
	int st_hint_x = 0;
	bool show_toast = false;
	std::string toast_cls, toast_text;

	bool show_sev = false, show_sinks = false, show_sessions = false, show_help = false, show_ctx = false, show_ctx_sub = false,
	     show_detail = false;
	std::vector<SevRow> sev_rows;
	std::string sinks_summary, sinks_search_cls, sinks_before, sinks_cursor_cls, sinks_under, sinks_after, sinks_placeholder;
	int sinks_list_h = 8;
	std::vector<SinkRow> sink_rows;
	bool sinks_empty = false;
	std::string sessions_summary, sess_search_cls, sess_before, sess_cursor_cls, sess_under, sess_after, sess_placeholder;
	int sess_list_h = 8;
	std::vector<SessionRow> sess_rows;
	bool sess_empty = false;
	int help_h = 20;
	std::vector<HelpRow> help_rows;
	int ctx_x = 0, ctx_y = 0, ctx_sub_x = 0, ctx_sub_y = 0;
	std::vector<CtxItem> ctx_items, ctx_sub_items;

	std::string dv_bar_cls, dv_sev_cls, dv_sev, dv_sink, dv_pos, dv_time, dv_file, dv_layout, dv_msg_focus, dv_bt_focus, dv_rule,
	    dv_bt_count, dv_bt_btn_cls;
	std::vector<TextLine> dv_msg_lines, dv_det_lines, dv_bt_lines;
	std::vector<BarCell> dv_msg_bar, dv_bt_bar;
	int dv_w = 80, dv_h = 24, dv_lw = 36, dv_lh = 10, dv_rw = 36, dv_rh = 10;
	bool dv_has_details = false, dv_has_bt = false;

	void MenuPick(std::string index);
	void MenuSession(std::string index);
	void RowClick(std::string index);
	void RowContext(std::string index);
	void BarJump(std::string index);
	void PaneBarMsg(std::string index);
	void PaneBarBt(std::string index);
	void FocusSearch();
	void ToggleGroup(std::string group);
	void ToggleFollow();
	void ToggleTime();
	void OpenSinks();
	void OpenLevels();
	void OpenHelp();
	void ClearClick();
	void BackdropClick();
	void Inside();
	void CloseOverlay();
	void SevRowClick(std::string index);
	void SinkRowClick(std::string index);
	void SinkToggle(std::string index);
	void SinkChip(std::string code);
	void FocusSinkSearch();
	void FocusSessSearch();
	void SessRowClick(std::string index);
	void CtxPick(std::string index);
	void CtxSubPick(std::string index);
	void FocusPane(std::string index);
	void DetailCopy();
	void DetailCopyMessage();
	void DetailCopyBacktrace();

private:
	static std::string_view viewText();

	struct Key {
		char32_t ch = 0;
		rtxui::Event::Keyboard::Special special = rtxui::Event::Keyboard::None;
		bool ctrl = false, alt = false, shift = false;
		const rtxui::Event::Keyboard* raw = nullptr;
		[[nodiscard]] bool is(char32_t c) const { return ch == c && !ctrl && !alt; }
		[[nodiscard]] bool isCtrl(char32_t c) const { return ch == c && ctrl; }
		[[nodiscard]] bool is(rtxui::Event::Keyboard::Special s) const { return special == s; }
	};

	void startLocalhost();
	void startLan(const Target& target);
	void startFile(const std::filesystem::path& path);
	void stopSource();
	void leaveToMenu();
	void enterMain();
	void onLinkState(Connection::State state);
	void onBatch(std::vector<RawRecord>&& batch);
	void resetStore(bool announce);
	void tick();
	void scheduleTick();
	void toast(std::string text, std::string kind = "");
	void refreshSize();
	void refreshFlags();
	void handleMouse(const rtxui::Event::Mouse& mouse);
	bool handleKey(const Key& key);
	bool handleTextKey(TextField& field, const Key& key, bool& focus, bool (App::*onChange)());
	void copyText(const std::string& text, const char* what);

	void buildRows();
	void buildBar();
	void buildStatus();
	void buildSearch();
	bool handleTableKey(const Key& key);
	void runChord(char first, char second, int count);
	void moveSel(long delta);
	void setSel(std::size_t position);
	void ensureVisible(bool center = false);
	void scrollBy(long delta);
	void goBottom();
	void followIfNeeded();
	[[nodiscard]] std::size_t viewRows() const;
	[[nodiscard]] const LogRecord* selected() const;
	bool searchChanged();
	void applyFilter(FilterState next, const char* undoLabel = nullptr);
	void toggleGroup(int group);
	void undoLast();
	void jumpToError(int direction);
	void clearAll();

	void buildMenu();
	void buildGrid();
	void randomizeBackground();
	void resetMenuIdle();
	void updateMenuIdle();
	void beginBackgroundChange();
	void beginBackgroundEntrance(bool startup = false);
	Clock::time_point menuNow() const;
	void refreshSessions();
	void activateMenu(int item);
	void beginLanEdit();
	bool handleMenuKey(const Key& key);
	bool lanChanged();
	void buildSessionsDialog();
	bool handleSessionsKey(const Key& key);
	bool sessSearchChanged();
	void openSession(const SessionInfo& session);

	void openOverlay(Overlay which);
	void closeOverlay();
	void buildSeverities();
	bool handleSeverityKey(const Key& key);
	void buildSinks();
	bool handleSinkKey(const Key& key);
	bool sinkSearchChanged();
	void toggleSinkRow(std::size_t listIndex);
	void toggleSinkGroup(StrId sink, int group);
	void soloSink(StrId sink);
	void invertSink(StrId sink);
	void buildHelp();
	bool handleHelpKey(const Key& key);
	void openContext(std::size_t position, int x, int y);
	void buildContext();
	bool handleContextKey(const Key& key);
	void runContext(int item);
	void runContextSub(int item);
	[[nodiscard]] std::vector<StrId> visibleSinks() const;

	void openDetail(std::size_t position);
	void buildDetail();
	bool handleDetailKey(const Key& key);
	void scrollPane(int pane, long delta);
	[[nodiscard]] int paneAt(int x, int y) const;
	[[nodiscard]] std::string recordText(const LogRecord& rec, bool includeBacktrace) const;

	Options options_;
	Store store_;
	std::unique_ptr<ViewIndex> index_;
	std::unique_ptr<Connection> connection_;
	std::unique_ptr<SessionScanner> scanner_;
	std::unique_ptr<CsvLoader> loader_;

	Source source_ = Source::None;
	Link link_ = Link::Searching;
	Target target_;
	std::string fileName_;
	double loadProgress_ = 1.0;
	bool menuAuto_ = true;
	bool tickStarted_ = false;
	std::function<void()> quit_;
	std::uint64_t sourceGen_ = 0;
	bool everConnected_ = false;

	int cols_ = 120, rowsTotal_ = 40;
	int mouseX_ = 1, mouseY_ = 1;
	bool consumed_ = false;
	bool contextRequested_ = false;
	std::size_t contextPosition_ = 0;
	std::uint64_t lastEpoch_ = ~0ull;
	std::size_t lastViewSize_ = ~std::size_t{0};

	int spinner_ = 0;
	std::uint64_t frame_ = 0;
	int backgroundAccent_ = -1;
	enum class IdleFade { None, Dissolve, FadeIn, Startup };
	IdleFade idleFade_ = IdleFade::None;
	Clock::time_point menuActivityAt_ = Clock::now(), idleFadeAt_ {};
	double backgroundCoverage_ = 1.0;
	double backgroundProgress_ = 1.0;
	Clock::time_point toastUntil_ {};
	Clock::time_point lastClick_ {};
	std::size_t lastClickPos_ = ~std::size_t{0};
	Clock::time_point lastSessionScan_ {};
	Clock::time_point chordAt_ {};

	std::size_t sel_ = 0, scrollTop_ = 0;
	bool follow_ = true;
	bool relativeTime_ = false;
	std::uint64_t firstTs_ = 0;
	TextField search_;
	bool searchFocus_ = false;
	std::string lastQueryText_;
	int count_ = 0;
	char chord_ = 0;
	std::vector<FilterState> undoStack_;
	std::vector<std::string> undoLabels_;
	bool clearedRecently_ = false;
	Clock::time_point clearedAt_ {};
	std::size_t selRecord_ = 0;
	bool haveSelRecord_ = false;

	Overlay overlay_ = Overlay::None;
	std::size_t sevSel_ = 0;
	TextField sinkSearch_;
	bool sinkSearchFocus_ = false;
	std::size_t sinkSel_ = 0, sinkTop_ = 0;
	std::vector<StrId> sinkList_;
	std::size_t helpTop_ = 0;
	std::size_t ctxSel_ = 0, ctxSubSel_ = 0, ctxPosition_ = 0;
	bool ctxInSub_ = false, ctxCopySub_ = false;
	int ctxW_ = 28;

	std::vector<SessionInfo> sessions_;
	int menuSel_ = 0;
	TextField lan_;
	bool lanFocus_ = false;
	TextField sessSearch_;
	bool sessSearchFocus_ = false;
	std::size_t sessSel_ = 0, sessTop_ = 0;
	std::vector<std::size_t> sessList_;

	std::size_t detailPos_ = 0;
	int detailPane_ = 0;
	bool detailDirty_ = true;
	std::vector<TextLine> dvMsgAll_, dvBtAll_;
	std::size_t dvMsgTop_ = 0, dvBtTop_ = 0;
	int dvMsgVis_ = 1, dvBtVis_ = 1, dvMsgWidth_ = 0, dvBtWidth_ = 0;
};

}  // namespace kenzo::ui
