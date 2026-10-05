// The playlist switcher (switcher_core.h). Derived from Better Tabs' TabsCore: the strip, the
// look, auto-hide, colours and menus are the same code; tabs are playlists and there is one child.

#include "switcher_core.h"

#include <commdlg.h>

#pragma comment(lib, "comdlg32.lib")

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

#include "../model/colour.h"
#include "../model/file_name.h"
#include "../model/title_fields.h"
#include "../platform/graphics.h"
#include "../platform/logging.h"
#include "../platform/perf.h"
#include "../playlists/playlist_title.h"
#include "../playlists/user_lock.h"
#include "host_util.h"

namespace ept {

namespace {

//! Every live element. Main thread.
std::vector<SwitcherCore*>& live_list() {
    static std::vector<SwitcherCore*> list;
    return list;
}

//! The tab list: one id per visible playlist (strip index).
//! Inside SwitcherCore::perf_test_playlists: the per-event perf lines are skipped.
bool g_perf_bulk = false;

constexpr unsigned menu_tab_base = 1;
//! Commands on the clicked tab, then one id per hidden playlist.
constexpr unsigned menu_cmd_base = 3000;
enum TabCommand : unsigned {
    cmd_new = menu_cmd_base,
    cmd_rename,
    cmd_remove,
    cmd_hide,
    cmd_move_back,
    cmd_move_forward,
    cmd_duplicate,
    cmd_save,
    cmd_lock,
    cmd_lock_ui,
    cmd_load,
    cmd_perf_create,
    cmd_perf_remove,
    cmd_configure,
};
constexpr unsigned menu_unhide_base = 3100;
//! The Appearance submenu.
constexpr unsigned menu_style_base = 5000;

//! Appends a list of items with ids id_base + i. Up to 30 stay flat; longer lists become
//! submenus of 25 named "first - last", the one holding `checked` ticked. Not columns:
//! dark popup menus cannot draw MF_MENUBARBREAK columns and fall back to the light theme.
void append_long_list(HMENU menu, const std::vector<std::wstring>& texts, UINT id_base, std::size_t max_items,
                      std::size_t checked) {
    const std::size_t n = (std::min)(texts.size(), max_items);
    constexpr std::size_t flat = 30, group = 25;
    if (n <= flat) {
        for (std::size_t i = 0; i < n; ++i) {
            AppendMenuW(menu, MF_STRING | (i == checked ? MF_CHECKED : 0), id_base + static_cast<UINT>(i), texts[i].c_str());
        }
        return;
    }
    for (std::size_t first = 0; first < n; first += group) {
        const std::size_t last = (std::min)(first + group, n) - 1;
        HMENU sub = CreatePopupMenu();
        if (sub == nullptr) return;
        for (std::size_t i = first; i <= last; ++i) {
            AppendMenuW(sub, MF_STRING | (i == checked ? MF_CHECKED : 0), id_base + static_cast<UINT>(i), texts[i].c_str());
        }
        const bool here = checked >= first && checked <= last;
        const std::wstring label = texts[first] + L"  \u2013  " + texts[last];
        AppendMenuW(menu, MF_POPUP | (here ? MF_CHECKED : 0), reinterpret_cast<UINT_PTR>(sub), label.c_str());
    }
}

enum StyleCommand : unsigned {
    style_position_top = menu_style_base,
    style_position_bottom,
    style_position_left,
    style_position_right,
    style_rotate_side_text,
    style_indicator_underline,
    style_indicator_pill,
    style_indicator_none,
    style_chip,
    style_accent_selection,
    style_accent_cover,
    style_accent_custom,
    style_sizing_fit,
    style_sizing_equal,
    style_sizing_fill,
    style_align_start,
    style_align_centre,
    style_align_end,
    style_chevron_start,
    style_shrink_titles,
    style_show_always,
    style_show_two_or_more,
    style_show_auto_hide,
    style_strength_auto,
    style_strength_subtle,
    style_strength_medium,
    style_strength_strong,
    style_strength_solid,
    style_background_theme,
    style_background_tint,
    style_background_custom,
    style_last,
};

// Auto-hide timers: the component's only timers, and only while something is due.
constexpr UINT_PTR timer_ah_delay = 0xB701;
constexpr UINT_PTR timer_ah_frame = 0xB702;
//! One-shot, only while a drag rests on a tab: switches to that tab's playlist.
constexpr UINT_PTR timer_drop_switch = 0xB703;

//! One DeferWindowPos batch that degrades to plain SetWindowPos calls if the batch fails.
class WindowMoves {
public:
    void add(HWND wnd, const RECT& rc, UINT flags) noexcept {
        if (wnd == nullptr || count_ == moves_.size()) return;
        moves_[count_++] = Move{wnd, rc, flags | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER};
    }
    void apply() noexcept {
        if (count_ == 0) return;
        HDWP dwp = BeginDeferWindowPos(static_cast<int>(count_));
        for (std::size_t i = 0; i < count_ && dwp != nullptr; ++i) {
            const Move& m = moves_[i];
            dwp = DeferWindowPos(dwp, m.wnd, nullptr, m.rc.left, m.rc.top, m.rc.right - m.rc.left,
                                 m.rc.bottom - m.rc.top, m.flags);
        }
        if (dwp != nullptr && EndDeferWindowPos(dwp)) {
            count_ = 0;
            return;
        }
        for (std::size_t i = 0; i < count_; ++i) {
            const Move& m = moves_[i];
            SetWindowPos(m.wnd, nullptr, m.rc.left, m.rc.top, m.rc.right - m.rc.left, m.rc.bottom - m.rc.top,
                         m.flags);
        }
        count_ = 0;
    }

private:
    struct Move {
        HWND wnd{nullptr};
        RECT rc{};
        UINT flags{0};
    };
    std::array<Move, 4> moves_{};
    std::size_t count_{0};
};

//! The system colour picker, seeded with and writing back 0xAARRGGBB. False if cancelled.
bool pick_colour(std::uint32_t& argb) noexcept {
    static COLORREF custom_colours[16]{};
    CHOOSECOLORW cc{sizeof(cc)};
    cc.hwndOwner = core_api::get_main_window();
    cc.rgbResult = colour::colorref_from_rgb(argb & 0xFFFFFFu);
    cc.lpCustColors = custom_colours;
    cc.Flags = CC_RGBINIT | CC_FULLOPEN;
    if (!ChooseColorW(&cc)) return false;
    argb = 0xFF000000u | colour::rgb_from_colorref(cc.rgbResult);
    return true;
}


// ---------------------------------------------------------------------------------------------
// Process-wide helpers, alive while at least one element has a window: one playback callback
// and one keyboard filter however many elements there are.

// Process-wide helpers, alive while at least one container has a window: one playback callback
// and one keyboard filter however many containers there are.

void broadcast(PlaybackEvent event) noexcept {
    // A copy: a handler may destroy an element (and so leave the list).
    const std::vector<SwitcherCore*> list = SwitcherCore::live();
    for (SwitcherCore* container : list) {
        if (std::find(SwitcherCore::live().begin(), SwitcherCore::live().end(), container) != SwitcherCore::live().end()) {
            container->on_playback(event);
        }
    }
}

//! Never from inside the play callback itself: following playback changes the active playlist,
//! which is a playlist manager call from inside a callback.
void post(PlaybackEvent event) noexcept {
    try {
        fb2k::inMainThread([event] { broadcast(event); });
    } catch (...) {
    }
}

class PlaybackWatch : public play_callback_impl_base {
public:
    PlaybackWatch()
        : play_callback_impl_base(flag_on_playback_starting | flag_on_playback_new_track | flag_on_playback_stop |
                                  flag_on_playback_pause | flag_on_playback_edited |
                                  flag_on_playback_dynamic_info_track) {}

    //! Playback the user started: play, next, previous, a double-clicked track. Not the next
    //! track of a playlist, and not the session resumed when foobar2000 starts.
    void on_playback_starting(play_control::t_track_command command, bool) override {
        if (command != play_control::track_command_resume) post(PlaybackEvent::started);
    }
    void on_playback_new_track(metadb_handle_ptr) override { post(PlaybackEvent::title); }
    void on_playback_stop(play_control::t_stop_reason reason) override {
        if (reason == play_control::stop_reason_starting_another) return; // a new track follows
        // Not while foobar2000 shuts down: that would create panels only to destroy them.
        if (reason != play_control::stop_reason_shutting_down) post(PlaybackEvent::stopped);
    }
    void on_playback_pause(bool) override { post(PlaybackEvent::title); }
    void on_playback_edited(metadb_handle_ptr) override { post(PlaybackEvent::title); }
    void on_playback_dynamic_info_track(const file_info&) override { post(PlaybackEvent::title); }
};

//! Ctrl+Tab and Ctrl+Shift+Tab anywhere inside a container. The innermost container that takes
//! it wins, so nested containers each cycle their own tabs.
class CtrlTabFilter : public message_filter_impl_base {
public:
    CtrlTabFilter() : message_filter_impl_base(WM_KEYDOWN, WM_KEYDOWN) {}

    bool pretranslate_message(MSG* msg) override {
        if (msg == nullptr || msg->message != WM_KEYDOWN || msg->wParam != VK_TAB) return false;
        if (GetKeyState(VK_CONTROL) >= 0 || GetKeyState(VK_MENU) < 0) return false;
        const int direction = GetKeyState(VK_SHIFT) < 0 ? -1 : 1;
        for (HWND wnd = msg->hwnd; wnd != nullptr; wnd = GetAncestor(wnd, GA_PARENT)) {
            for (SwitcherCore* container : SwitcherCore::live()) {
                if (container->core_wnd() == wnd) {
                    if (container->cycle_tabs(direction)) return true;
                    break;
                }
            }
            if ((GetWindowLongPtrW(wnd, GWL_STYLE) & WS_CHILD) == 0) break;
        }
        return false;
    }
};

struct Shared {
    unsigned windows{0};
    std::unique_ptr<PlaybackWatch> playback;
    std::unique_ptr<CtrlTabFilter> keys;
};

Shared& shared() {
    static Shared instance;
    return instance;
}

void attach_shared() noexcept {
    Shared& s = shared();
    if (s.windows++ != 0) return;
    try {
        s.playback = std::make_unique<PlaybackWatch>();
        s.keys = std::make_unique<CtrlTabFilter>();
    } catch (const std::exception& e) {
        log::warn(std::string("could not watch playback or keys: ") + e.what());
    }
}

void detach_shared() noexcept {
    Shared& s = shared();
    if (s.windows == 0 || --s.windows != 0) return;
    s.playback.reset();
    s.keys.reset();
}

} // namespace

const std::vector<SwitcherCore*>& SwitcherCore::live() noexcept { return live_list(); }

SwitcherCore::SwitcherCore() { live_list().push_back(this); }

SwitcherCore::~SwitcherCore() {
    std::erase(live_list(), this);
    if (subscribed_) playlists::unsubscribe(*this);
    ah_sync_parent_watch();
}

// ---------------------------------------------------------------------------------------------
// Configuration.

void SwitcherCore::load_settings(Settings settings, std::vector<RawField> unknown_settings) {
    settings_ = std::move(settings);
    clamp(settings_);
    unknown_settings_ = std::move(unknown_settings);
}

void SwitcherCore::reload_settings(Settings settings, std::vector<RawField> unknown_settings) {
    load_settings(std::move(settings), std::move(unknown_settings));
    if (core_wnd() == nullptr) return;
    title_script_.release();
    title_source_.clear();
    sync_title_needs();
    rebuild_strip();
    apply_settings();
}

// ---------------------------------------------------------------------------------------------
// The child.

void SwitcherCore::host_query_limits(Limits& out) noexcept {
    out = Limits{};
    const HWND child = host_child_wnd();
    if (child == nullptr) return;
    MINMAXINFO mmi{};
    mmi.ptMaxTrackSize.x = MAXLONG;
    mmi.ptMaxTrackSize.y = MAXLONG;
    SendMessageW(child, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&mmi));
    out.min_width = static_cast<unsigned>(std::clamp(mmi.ptMinTrackSize.x, 0L, limit_cap));
    out.min_height = static_cast<unsigned>(std::clamp(mmi.ptMinTrackSize.y, 0L, limit_cap));
    out.max_width = static_cast<unsigned>(std::clamp(mmi.ptMaxTrackSize.x, 0L, limit_cap));
    out.max_height = static_cast<unsigned>(std::clamp(mmi.ptMaxTrackSize.y, 0L, limit_cap));
}

void SwitcherCore::child_changed() noexcept {
    child_seen_ = nullptr; // place it even if the rectangle is unchanged
    host_query_limits(child_limits_);
    if (core_wnd() == nullptr) return;
    layout();
    // A newly created child window starts on top of the z-order, above the auto-hide windows.
    if (auto_hide()) ah_raise();
    limits_changed();
}

void SwitcherCore::on_child_limits_changed() noexcept {
    host_query_limits(child_limits_);
    limits_changed();
    layout();
}

// ---------------------------------------------------------------------------------------------
// Playlists.

bool SwitcherCore::format_titles() const noexcept {
    return settings_.title_mode == TitleMode::format && !settings_.title_format.empty();
}

bool SwitcherCore::titles_follow_position() const noexcept { return (title_fields_ & title_field_index) != 0; }

void SwitcherCore::sync_title_needs() noexcept {
    title_fields_ = format_titles() ? title_fields(settings_.title_format) : 0;
    std::uint32_t needs = 0;
    if ((title_fields_ & title_field_size) != 0) needs |= playlists::need_counts;
    if ((title_fields_ & title_field_length) != 0) needs |= playlists::need_lengths;
    if (subscribed_) playlists::set_needs(*this, needs);
    playing_key_ = 0;
    try {
        const std::size_t playing = playlist_manager::get()->get_playing_playlist();
        const auto& entries = playlists::entries();
        if (playing < entries.size()) playing_key_ = entries[playing].key;
    } catch (...) {
    }
}

const std::wstring& SwitcherCore::label_of(std::size_t playlist) const noexcept {
    static const std::wstring none;
    const auto& entries = playlists::entries();
    if (playlist >= entries.size()) return none;
    if (format_titles()) {
        const auto it = labels_.find(entries[playlist].key);
        return it != labels_.end() ? it->second : none;
    }
    return entries[playlist].name;
}

bool SwitcherCore::relabel_if_changed(std::size_t playlist) noexcept {
    const auto& entries = playlists::entries();
    if (!format_titles() || playlist >= entries.size()) return false;
    const auto it = labels_.find(entries[playlist].key);
    std::wstring before = it != labels_.end() ? it->second : std::wstring();
    update_label(playlist);
    if (label_of(playlist) == before) return false;
    strip_playlist_relabel(playlist);
    return true;
}

StripItem SwitcherCore::make_item(std::size_t playlist) const {
    StripItem item;
    const auto& entries = playlists::entries();
    if (playlist < entries.size()) item.key = entries[playlist].key;
    item.label = label_of(playlist); // the tooltip stays empty: the strip shows the label
    return item;
}

void SwitcherCore::update_label(std::size_t playlist) noexcept {
    const auto& entries = playlists::entries();
    if (!format_titles() || playlist >= entries.size()) return;
    std::wstring& label = labels_[entries[playlist].key];
    try {
        if (!title_script_.is_valid() || title_source_ != settings_.title_format) {
            title_script_.release();
            titleformat_compiler::get()->compile_safe_ex(title_script_, settings_.title_format.c_str(),
                                                         "(invalid title)");
            title_source_ = settings_.title_format;
        }
        label = playlists::format_title(title_script_, playlist, entries[playlist], playlists::active(),
                                        playlist_manager::get()->get_playing_playlist());
    } catch (...) {
        label = entries[playlist].name;
    }
}

void SwitcherCore::prune_labels() noexcept {
    try {
        std::unordered_map<std::uint64_t, std::wstring> kept;
        kept.reserve(playlists::entries().size());
        for (const auto& e : playlists::entries()) {
            if (auto it = labels_.find(e.key); it != labels_.end()) kept.emplace(e.key, std::move(it->second));
        }
        labels_.swap(kept);
    } catch (...) {
    }
}

void SwitcherCore::rebuild_strip() noexcept {
    const std::uint64_t t_start = perf::enabled() ? perf::now() : 0;
    const auto& entries = playlists::entries();
    try {
        labels_.clear();
        if (format_titles()) {
            labels_.reserve(entries.size());
            for (std::size_t i = 0; i < entries.size(); ++i) update_label(i);
        }
        visible_.clear();
        items_.resize(0);
        visible_.reserve(entries.size());
        items_.reserve(entries.size());
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].hidden()) continue;
            visible_.push_back(i);
            items_.push_back(make_item(i));
        }
    } catch (...) {
        visible_.clear();
        items_.clear();
    }
    // Keyed: tabs whose title did not change keep their layouts.
    strip_.set_items(items_, strip_index_of_playlist(playlists::active()));
    items_.clear(); // the strip owns the copies; keep only the capacity
    if (t_start != 0 && !in_playlist_event_) {
        pfc::string_formatter f;
        f << "strip rebuilt: " << pfc::format_uint(strip_.item_count()) << " tabs of "
          << pfc::format_uint(entries.size()) << " playlists in "
          << pfc::format_float(perf::elapsed_ms(t_start, perf::now()), 0, 3) << " ms, "
          << pfc::format_uint(strip_.take_layouts_built()) << " layouts built";
        log::info(f.get_ptr());
    }
}

std::size_t SwitcherCore::strip_index_of_playlist(std::size_t playlist) const noexcept {
    if (playlist == SIZE_MAX) return no_index;
    // visible_ is sorted: binary search.
    const auto it = std::lower_bound(visible_.begin(), visible_.end(), playlist);
    return it != visible_.end() && *it == playlist ? static_cast<std::size_t>(it - visible_.begin()) : no_index;
}

std::size_t SwitcherCore::playlist_of_strip(std::size_t strip_index) const noexcept {
    return strip_index < visible_.size() ? visible_[strip_index] : SIZE_MAX;
}

void SwitcherCore::activate_playlist(std::size_t playlist, bool from_user) noexcept {
    if (playlist >= playlists::entries().size()) return;
    const std::uint64_t t_start = perf::enabled() ? perf::now() : 0;
    if (playlist != playlists::active()) {
        // The playlist manager calls us (and every other element) back with on_playlist_activate;
        // the strip follows from there, so all elements stay in step.
        try {
            playlist_manager::get()->set_active_playlist(playlist);
        } catch (...) {
            return;
        }
    }
    if (from_user) ah_note_switch();
    if (t_start != 0) {
        const HWND child = host_child_wnd();
        const std::uint64_t t_switched = perf::now();
        if (child != nullptr) UpdateWindow(child);
        const std::uint64_t t_painted = perf::now();
        perf::PaintStats strip_stats;
        strip_.take_paint_stats(strip_stats);
        pfc::string_formatter f;
        f << "switch to playlist " << pfc::format_uint(playlist + 1) << " of "
          << pfc::format_uint(playlists::entries().size()) << ": "
          << pfc::format_float(perf::elapsed_ms(t_start, t_switched), 0, 3) << " ms, child painted after "
          << pfc::format_float(perf::elapsed_ms(t_start, t_painted), 0, 3) << " ms";
        if (strip_stats.paints != 0) {
            f << "; strip: " << pfc::format_uint(strip_stats.paints) << " paints, worst "
              << pfc::format_float(strip_stats.worst_ms, 0, 3) << " ms, " << pfc::format_uint(strip_stats.pixels)
              << " px, " << pfc::format_uint(strip_stats.allocating_paints) << " allocating";
        }
        log::info(f.get_ptr());
    }
}

std::size_t SwitcherCore::fallback_visible(std::size_t from) const noexcept {
    const auto& entries = playlists::entries();
    for (std::size_t i = from; i < entries.size(); ++i) {
        if (!entries[i].hidden()) return i;
    }
    for (std::size_t i = (std::min)(from, entries.size()); i > 0; --i) {
        if (!entries[i - 1].hidden()) return i - 1;
    }
    return SIZE_MAX;
}

void SwitcherCore::set_playlist_hidden(std::size_t playlist, bool hidden) noexcept {
    const auto& entries = playlists::entries();
    if (playlist >= entries.size() || entries[playlist].hidden() == hidden) return;
    // The last visible tab stays: an empty strip could only be repaired from another element.
    if (hidden && visible_.size() < 2) return;
    const bool was_active = playlist == playlists::active();
    playlists::set_hidden(playlist, hidden); // every element hears of it (on_playlists)
    if (hidden && was_active) {
        activate_playlist(fallback_visible(playlist + 1), false);
    } else if (!hidden) {
        activate_playlist(playlist, true);
    }
}

bool SwitcherCore::playlist_allows(std::size_t playlist, std::uint32_t lock_filter) const noexcept {
    try {
        auto pm = playlist_manager::get();
        if (playlist >= pm->get_playlist_count()) return false;
        return (pm->playlist_lock_get_filter_mask(playlist) & lock_filter) == 0;
    } catch (...) {
        return false;
    }
}

void SwitcherCore::move_playlist(std::size_t from, std::size_t to) noexcept {
    try {
        auto pm = playlist_manager::get();
        const std::size_t count = pm->get_playlist_count();
        if (from >= count || to >= count || from == to) return;
        // new[i] = old[order[i]]: take `from` out, put it back at `to`.
        std::vector<t_size> order(count);
        std::size_t src = 0;
        for (std::size_t i = 0; i < count; ++i) {
            if (i == to) {
                order[i] = from;
                continue;
            }
            if (src == from) ++src;
            order[i] = src++;
        }
        if (!pm->reorder(order.data(), count)) rebuild_strip();
    } catch (...) {
        rebuild_strip();
    }
}

void SwitcherCore::rename_playlist(std::size_t playlist) noexcept {
    if (playlist >= playlists::entries().size()) return;
    if (!playlist_allows(playlist, playlist_lock::filter_rename)) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    const auto keep_alive = host_keep_alive();
    try {
        const std::wstring original = playlists::entries()[playlist].name;
        std::wstring name = original;
        menu_pin_ = true;
        const bool ok = run_rename_dialog(core_wnd(), name);
        menu_pin_ = false;
        ah_evaluate();
        // The playlists may have changed while the dialog was open: rename only if the playlist
        // at that position is still the one we started from.
        if (!ok || playlist >= playlists::entries().size() || playlists::entries()[playlist].name != original) return;
        const pfc::string8 utf8 = narrow(name);
        playlist_manager::get()->playlist_rename(playlist, utf8.get_ptr(), utf8.get_length());
    } catch (const std::exception& e) {
        menu_pin_ = false;
        log::warn(std::string("rename failed: ") + e.what());
    }
}

void SwitcherCore::remove_playlist(std::size_t playlist) noexcept {
    try {
        auto pm = playlist_manager::get();
        if (playlist >= pm->get_playlist_count()) return;
        // Refuses the last playlist and locked ones, beeping itself; File > Restore brings it back.
        (void)pm->remove_playlist_user(playlist);
    } catch (...) {
    }
}

void SwitcherCore::new_playlist(std::size_t at) noexcept {
    try {
        auto pm = playlist_manager::get();
        // "New Playlist", "New Playlist (2)", ... - the playlist manager's own naming.
        const std::size_t index = pm->create_playlist_autoname(at);
        if (index != SIZE_MAX) pm->set_active_playlist(index);
        ah_note_switch();
    } catch (...) {
    }
}

void SwitcherCore::strip_structure_changed() noexcept {
    layout();
    limits_changed();
}

void SwitcherCore::strip_playlist_created(std::size_t playlist) noexcept {
    const auto& entries = playlists::entries();
    if (playlist >= entries.size()) return rebuild_strip();
    try {
        if (format_titles()) {
            if (titles_follow_position()) return rebuild_strip(); // keyed: unchanged titles keep layouts
            update_label(playlist);
        }
        auto it = std::lower_bound(visible_.begin(), visible_.end(), playlist);
        for (auto j = it; j != visible_.end(); ++j) ++*j;
        if (entries[playlist].hidden()) {
            strip_.set_active(strip_index_of_playlist(playlists::active()));
            return;
        }
        const std::size_t pos = static_cast<std::size_t>(it - visible_.begin());
        visible_.insert(it, playlist);
        strip_.insert_item(pos, make_item(playlist), strip_index_of_playlist(playlists::active()));
    } catch (...) {
        rebuild_strip();
    }
}

void SwitcherCore::strip_playlist_removed(std::size_t playlist) noexcept {
    try {
        // Labels are keyed: the removed playlist's title stays until the map is pruned.
        if (format_titles() && labels_.size() > 2 * playlists::entries().size() + 16) prune_labels();
        auto it = std::lower_bound(visible_.begin(), visible_.end(), playlist);
        const bool was_visible = it != visible_.end() && *it == playlist;
        const std::size_t pos = static_cast<std::size_t>(it - visible_.begin());
        if (was_visible) it = visible_.erase(it);
        for (auto j = it; j != visible_.end(); ++j) --*j;
        if (titles_follow_position()) return rebuild_strip();
        if (was_visible) {
            strip_.erase_item(pos, strip_index_of_playlist(playlists::active()));
        } else {
            strip_.set_active(strip_index_of_playlist(playlists::active()));
        }
    } catch (...) {
        rebuild_strip();
    }
}

void SwitcherCore::strip_playlists_reordered() noexcept {
    // Titles that show the position change (keyed, so only changed titles build layouts).
    // Other titles are keyed by playlist and simply move along.
    if (titles_follow_position()) return rebuild_strip();
    const auto& entries = playlists::entries();
    try {
        visible_.clear();
        strip_keys_.clear();
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].hidden()) continue;
            visible_.push_back(i);
            strip_keys_.push_back(entries[i].key);
        }
        if (strip_.reorder_items(strip_keys_, strip_index_of_playlist(playlists::active()))) return;
    } catch (...) {
    }
    rebuild_strip();
}

void SwitcherCore::strip_playlist_flags(std::size_t playlist) noexcept {
    const auto& entries = playlists::entries();
    if (playlist >= entries.size()) return rebuild_strip();
    try {
        const bool hidden = entries[playlist].hidden();
        auto it = std::lower_bound(visible_.begin(), visible_.end(), playlist);
        const bool present = it != visible_.end() && *it == playlist;
        const std::size_t pos = static_cast<std::size_t>(it - visible_.begin());
        if (hidden && present) {
            visible_.erase(it);
            strip_.erase_item(pos, strip_index_of_playlist(playlists::active()));
        } else if (!hidden && !present) {
            visible_.insert(it, playlist);
            strip_.insert_item(pos, make_item(playlist), strip_index_of_playlist(playlists::active()));
        }
    } catch (...) {
        rebuild_strip();
    }
}

void SwitcherCore::strip_playlist_relabel(std::size_t playlist) noexcept {
    const std::size_t pos = strip_index_of_playlist(playlist);
    if (pos == no_index) return;
    try {
        const int thickness = strip_.thickness();
        strip_.update_item(pos, make_item(playlist));
        if (strip_.thickness() != thickness) layout(); // a side strip follows its widest title
    } catch (...) {
        rebuild_strip();
    }
}

void SwitcherCore::on_playlists(playlists::Change change, std::size_t index) noexcept {
    if (core_wnd() == nullptr) return;
    using playlists::Change;
    const std::uint64_t t_start =
        perf::enabled() && change != Change::activated && !g_perf_bulk ? perf::now() : 0;
    in_playlist_event_ = true;
    const char* what = "reset";
    switch (change) {
    case Change::activated: {
        // The cheap path: two tabs repaint. Titles that show %is_active% change too.
        const std::size_t active = playlists::active();
        if (active != SIZE_MAX && active < playlists::entries().size() && playlists::entries()[active].hidden()) {
            // A hidden playlist was activated from elsewhere (the Playlist Manager): show it.
            playlists::set_hidden(active, false);
            break;
        }
        if ((title_fields_ & title_field_active) != 0) {
            for (const std::size_t p : {index, active}) (void)relabel_if_changed(p);
        }
        strip_.set_active(strip_index_of_playlist(active));
        break;
    }
    case Change::renamed:
    case Change::locked:
        what = change == Change::renamed ? "renamed" : "locked";
        if (format_titles()) {
            (void)relabel_if_changed(index);
        } else if (change == Change::renamed) {
            strip_playlist_relabel(index); // a playlist name does not show the lock
        }
        break;
    case Change::items:
        // Only arrives while %size% or %length% is shown (sync_title_needs), coalesced.
        what = "tracks changed";
        (void)relabel_if_changed(index);
        break;
    case Change::created:
        what = "created";
        strip_playlist_created(index);
        strip_structure_changed();
        break;
    case Change::removed:
        what = "removed";
        strip_playlist_removed(index);
        strip_structure_changed();
        break;
    case Change::reordered:
        what = "reordered";
        strip_playlists_reordered();
        break;
    case Change::flags:
        what = "hidden/shown";
        strip_playlist_flags(index);
        strip_structure_changed();
        break;
    case Change::reset:
    default:
        rebuild_strip();
        strip_structure_changed();
        break;
    }
    in_playlist_event_ = false;
    if (t_start != 0) {
        pfc::string_formatter f;
        f << "playlists " << what << ": strip updated in " << pfc::format_float(perf::elapsed_ms(t_start, perf::now()), 0, 3)
          << " ms, " << pfc::format_uint(strip_.take_layouts_built()) << " layouts built, "
          << pfc::format_uint(strip_.item_count()) << " tabs of " << pfc::format_uint(playlists::entries().size())
          << " playlists";
        log::info(f.get_ptr());
    }
}

// ---------------------------------------------------------------------------------------------
// Drag and drop onto the strip: hover a tab to switch to it, drop on a tab to add to that
// playlist, drop on empty space for a new playlist.

void SwitcherCore::sync_hot_zone_drop() noexcept {
    // Registered once per hot zone window; StripDrop::attach replaces an older registration.
    if (hot_zone_.hwnd() == nullptr) {
        hot_zone_drop_.detach();
        return;
    }
    if (!hot_zone_drop_.attached_to(hot_zone_.hwnd())) (void)hot_zone_drop_.attach(hot_zone_.hwnd(), hot_zone_drop_handler_);
}

DWORD SwitcherCore::HotZoneDrop::on_drop_enter(IDataObject*, POINT, DWORD) noexcept {
    core_.drop_hovering_ = true;
    core_.ah_evaluate(); // pinned: reveals after the reveal delay (timers run during a drag)
    return DROPEFFECT_NONE;
}

DWORD SwitcherCore::HotZoneDrop::on_drop_over(POINT, DWORD) noexcept { return DROPEFFECT_NONE; }

void SwitcherCore::HotZoneDrop::on_drop_leave() noexcept {
    // Usually because the strip appeared under the pointer; its DragEnter pins again.
    core_.drop_hovering_ = false;
    core_.ah_evaluate();
}

DWORD SwitcherCore::HotZoneDrop::on_drop(IDataObject*, POINT, DWORD) noexcept {
    on_drop_leave();
    return DROPEFFECT_NONE;
}

DWORD SwitcherCore::on_drop_enter(IDataObject* data, POINT screen, DWORD allowed) noexcept {
    drop_end();
    drop_hovering_ = true;
    if (auto_hide()) ah_evaluate(); // cancels a pending hide
    try {
        drop_ok_ = data != nullptr && playlist_incoming_item_filter::get()->process_dropped_files_check(data);
    } catch (...) {
        drop_ok_ = false;
    }
    return drop_track(screen, allowed);
}

DWORD SwitcherCore::on_drop_over(POINT screen, DWORD allowed) noexcept { return drop_track(screen, allowed); }

void SwitcherCore::on_drop_leave() noexcept {
    drop_end();
    drop_after_auto_hide();
}

DWORD SwitcherCore::drop_track(POINT screen, DWORD allowed) noexcept {
    const HWND strip = strip_.hwnd();
    // Always a copy: a move would let Explorer delete the files it dragged.
    if (!drop_ok_ || strip == nullptr || (allowed & DROPEFFECT_COPY) == 0) return DROPEFFECT_NONE;
    POINT pt = screen;
    ScreenToClient(strip, &pt);
    RECT client{};
    GetClientRect(strip, &client);
    const bool inside = PtInRect(&client, pt) != FALSE;
    const bool chevron = inside && strip_.chevron_at(pt);
    const std::size_t tab = inside && !chevron ? strip_.tab_at(pt) : no_index;
    if (tab != drop_tab_) {
        drop_tab_ = tab;
        strip_.set_drop_hover(tab);
        const HWND self = core_wnd();
        if (self != nullptr) {
            KillTimer(self, timer_drop_switch);
            if (tab != no_index && playlist_of_strip(tab) != playlists::active()) {
                UINT hover_ms = 400;
                SystemParametersInfoW(SPI_GETMOUSEHOVERTIME, 0, &hover_ms, 0);
                SetTimer(self, timer_drop_switch, std::clamp(hover_ms, 200u, 2000u) + 200u, nullptr);
            }
        }
    }
    if (!inside || chevron) return DROPEFFECT_NONE;
    if (tab == no_index) return DROPEFFECT_COPY; // a new playlist
    return playlist_allows(playlist_of_strip(tab), playlist_lock::filter_add) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
}

void SwitcherCore::drop_after_auto_hide() noexcept {
    drop_hovering_ = false;
    if (!auto_hide()) return;
    // No mouse messages reached the strip during the drag: re-arm its leave tracking, then
    // let auto-hide decide (hides after the hide delay if the pointer is elsewhere).
    if (strip_.hwnd() != nullptr && IsWindowVisible(strip_.hwnd()) != FALSE) strip_.track_pointer();
    ah_evaluate();
}

void SwitcherCore::drop_end() noexcept {
    if (const HWND self = core_wnd(); self != nullptr) KillTimer(self, timer_drop_switch);
    if (drop_tab_ != no_index) strip_.set_drop_hover(no_index);
    drop_tab_ = no_index;
    drop_ok_ = false;
}

void SwitcherCore::on_drop_switch_timer() noexcept {
    if (const HWND self = core_wnd(); self != nullptr) KillTimer(self, timer_drop_switch);
    const std::size_t playlist = playlist_of_strip(drop_tab_);
    if (playlist != SIZE_MAX && playlist != playlists::active()) activate_playlist(playlist, true);
}

DWORD SwitcherCore::on_drop(IDataObject* data, POINT screen, DWORD allowed) noexcept {
    const DWORD effect = drop_track(screen, allowed);
    const std::size_t tab = drop_tab_;
    drop_end();
    drop_after_auto_hide();
    if (effect == DROPEFFECT_NONE || data == nullptr) return DROPEFFECT_NONE;
    try {
        process_locations_notify::ptr notify;
        if (tab != no_index) {
            // By key: the playlists may move while the files are read.
            const std::size_t playlist = playlist_of_strip(tab);
            if (playlist >= playlists::entries().size()) return DROPEFFECT_NONE;
            const std::uint64_t key = playlists::entries()[playlist].key;
            notify = process_locations_notify::create([key](metadb_handle_list_cref items) {
                try {
                    const std::size_t index = playlists::index_of_key(key);
                    auto pm = playlist_manager::get();
                    if (index == SIZE_MAX || index >= pm->get_playlist_count() || items.get_count() == 0) return;
                    if ((pm->playlist_lock_get_filter_mask(index) & playlist_lock::filter_add) != 0) {
                        MessageBeep(MB_ICONWARNING);
                        return;
                    }
                    pm->playlist_add_items(index, items, pfc::bit_array_false());
                } catch (...) {
                }
            });
        } else {
            // The name is read now: the data object is only valid during the drop.
            const std::string name = settings_.drop_name == DropName::folder ? dropped_folder_name(data) : std::string();
            notify = process_locations_notify::create([name](metadb_handle_list_cref items) {
                try {
                    if (items.get_count() == 0) return;
                    auto pm = playlist_manager::get();
                    const std::size_t index = name.empty() ? pm->create_playlist_autoname(SIZE_MAX)
                                                           : pm->create_playlist(name.c_str(), SIZE_MAX, SIZE_MAX);
                    if (index == SIZE_MAX) return;
                    pm->playlist_add_items(index, items, pfc::bit_array_false());
                    pm->set_active_playlist(index);
                } catch (...) {
                }
            });
        }
        // The main window owns the progress dialog: this element may be gone before it ends.
        playlist_incoming_item_filter_v2::get()->process_dropped_files_async(
            data, playlist_incoming_item_filter_v2::op_flag_delay_ui, core_api::get_main_window(), notify);
        if (perf::enabled()) {
            log::info(tab != no_index ? "drop: adding to a playlist" : "drop: new playlist");
        }
    } catch (const std::exception& e) {
        log::warn(std::string("drop failed: ") + e.what());
        return DROPEFFECT_NONE;
    }
    return DROPEFFECT_COPY;
}

void SwitcherCore::on_playback(PlaybackEvent event) noexcept {
    if (core_wnd() == nullptr) return;
    if (event == PlaybackEvent::started && settings_.follow_playing) {
        try {
            const std::size_t playing = playlist_manager::get()->get_playing_playlist();
            if (playing != SIZE_MAX && playing != playlists::active()) activate_playlist(playing, false);
        } catch (...) {
        }
    }
    if ((title_fields_ & title_field_playing) == 0) return;
    // Two tabs at most: the one that stopped playing and the one that started.
    try {
        const auto& entries = playlists::entries();
        const std::size_t playing = playlist_manager::get()->get_playing_playlist();
        const std::uint64_t key = playing < entries.size() ? entries[playing].key : 0;
        if (key == playing_key_) return;
        const std::size_t old = playlists::index_of_key(playing_key_);
        playing_key_ = key;
        if (old != SIZE_MAX) (void)relabel_if_changed(old);
        if (playing < entries.size()) (void)relabel_if_changed(playing);
    } catch (...) {
    }
}

bool SwitcherCore::cycle_tabs(int direction) noexcept {
    if (!settings_.ctrl_tab || visible_.size() < 2) return false;
    const std::size_t count = visible_.size();
    const std::size_t current = strip_index_of_playlist(playlists::active());
    std::size_t next = 0;
    if (current != no_index) next = direction > 0 ? (current + 1) % count : (current + count - 1) % count;
    activate_playlist(visible_[next], true);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Layout and size limits.

bool SwitcherCore::want_strip() const noexcept {
    switch (settings_.visibility) {
    case StripVisibility::never: return false;
    case StripVisibility::two_or_more: return visible_.size() >= 2;
    case StripVisibility::auto_hide: return ah_shown_ || ah_animating_;
    case StripVisibility::always:
    default: return true;
    }
}

void SwitcherCore::layout() noexcept {
    const HWND self = core_wnd();
    if (self == nullptr) return;
    RECT client{};
    GetClientRect(self, &client);
    const bool show = want_strip() && strip_.hwnd() != nullptr;
    const bool ah = auto_hide() && strip_.hwnd() != nullptr;
    RECT strip_rc{};
    content_ = client;
    if (show || ah) {
        const bool across_height =
            settings_.position == StripPosition::top || settings_.position == StripPosition::bottom;
        const int room = across_height ? client.bottom : client.right;
        const int t = (std::min)(strip_.thickness(), (std::max)(0, room));
        switch (settings_.position) {
        case StripPosition::top:
            strip_rc = RECT{0, 0, client.right, t};
            content_.top = t;
            break;
        case StripPosition::bottom:
            strip_rc = RECT{0, client.bottom - t, client.right, client.bottom};
            content_.bottom = client.bottom - t;
            break;
        case StripPosition::left:
            strip_rc = RECT{0, 0, t, client.bottom};
            content_.left = t;
            break;
        case StripPosition::right:
            strip_rc = RECT{client.right - t, 0, client.right, client.bottom};
            content_.right = client.right - t;
            break;
        }
    }

    // Auto-hide: over the panel the content keeps the whole client area and the strip slides or
    // fades as a layered child; pushing shrinks the content only while the strip is shown. The
    // hot zone lies along the edge while the strip is hidden, over the panel when it is layered,
    // else in room taken from the panel.
    RECT hot_rc{};
    bool hot_show = false;
    if (ah) {
        const bool overlay = ah_overlay();
        const int hz = (std::max)(1, ah_px(settings_.hot_zone));
        switch (settings_.position) {
        case StripPosition::top: hot_rc = RECT{0, 0, client.right, hz}; break;
        case StripPosition::bottom: hot_rc = RECT{0, client.bottom - hz, client.right, client.bottom}; break;
        case StripPosition::left: hot_rc = RECT{0, 0, hz, client.bottom}; break;
        case StripPosition::right: hot_rc = RECT{client.right - hz, 0, client.right, client.bottom}; break;
        }
        hot_show = hot_zone_.hwnd() != nullptr && !show;
        if (overlay || !ah_shown_) content_ = client;
        if (hot_show && !hot_zone_.layered()) {
            switch (settings_.position) {
            case StripPosition::top: content_.top = hot_rc.bottom; break;
            case StripPosition::bottom: content_.bottom = hot_rc.top; break;
            case StripPosition::left: content_.left = hot_rc.right; break;
            case StripPosition::right: content_.right = hot_rc.left; break;
            }
        }
        if (overlay && show && ah_animating_) {
            const float p = ah_progress_;
            if (settings_.show_hide_animation == ShowHideAnimation::slide) {
                const bool across_height =
                    settings_.position == StripPosition::top || settings_.position == StripPosition::bottom;
                const int t = across_height ? strip_rc.bottom - strip_rc.top : strip_rc.right - strip_rc.left;
                const int off = static_cast<int>(std::lround((1.0f - p) * static_cast<float>(t)));
                switch (settings_.position) {
                case StripPosition::top: OffsetRect(&strip_rc, 0, -off); break;
                case StripPosition::bottom: OffsetRect(&strip_rc, 0, off); break;
                case StripPosition::left: OffsetRect(&strip_rc, -off, 0); break;
                case StripPosition::right: OffsetRect(&strip_rc, off, 0); break;
                }
            } else {
                strip_.set_alpha(static_cast<BYTE>(std::lround(255.0f * std::clamp(p, 0.0f, 1.0f))));
            }
        } else if (strip_.layered()) {
            strip_.set_alpha(255);
        }
    }

    const bool strip_was_shown = strip_.hwnd() != nullptr && IsWindowVisible(strip_.hwnd());
    WindowMoves moves;
    if (strip_.hwnd() != nullptr) {
        if (show) {
            moves.add(strip_.hwnd(), strip_rc, SWP_SHOWWINDOW);
        } else if (strip_was_shown) {
            strip_.forget_pointer();
            moves.add(strip_.hwnd(), strip_rc, SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE);
        }
    }
    if (hot_zone_.hwnd() != nullptr) {
        if (hot_show) {
            moves.add(hot_zone_.hwnd(), hot_rc, SWP_SHOWWINDOW);
        } else if (IsWindowVisible(hot_zone_.hwnd())) {
            hot_zone_.forget_pointer();
            moves.add(hot_zone_.hwnd(), hot_rc, SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE);
        }
    }
    const HWND child = host_child_wnd();
    if (child != nullptr && (child != child_seen_ || !same_rect(child_applied_, content_))) {
        child_seen_ = child;
        child_applied_ = content_;
        moves.add(child, content_, 0);
    }
    moves.apply();
    if (ah) ah_raise();

    if (ah && strip_was_shown && !show && perf::enabled() && child != nullptr) {
        // Hiding over the child must not make it repaint. Measured here.
        RECT dirty{};
        const bool invalidated = GetUpdateRect(child, &dirty, FALSE) != FALSE;
        pfc::string_formatter f;
        f << "auto-hide: strip hidden (" << (ah_overlay() ? "over the panel" : "push") << "), panel "
          << (invalidated ? "invalidated" : "not invalidated");
        log::info(f.get_ptr());
    }

    if (show != strip_shown_) {
        strip_shown_ = show;
        limits_changed();
    }
}

Limits SwitcherCore::compute_limits() const noexcept {
    Limits out = child_limits_;
    out.max_width = (std::max)(out.max_width, out.min_width);
    out.max_height = (std::max)(out.max_height, out.min_height);
    // An auto-hidden strip never counts: showing it must not resize the layout around us.
    if (strip_shown_ && !auto_hide()) {
        const auto t = static_cast<unsigned>((std::max)(0, strip_.thickness()));
        const bool vertical_stack =
            settings_.position == StripPosition::top || settings_.position == StripPosition::bottom;
        unsigned& min_along = vertical_stack ? out.min_height : out.min_width;
        unsigned& max_along = vertical_stack ? out.max_height : out.max_width;
        min_along = (std::min)(min_along + t, static_cast<unsigned>(limit_cap));
        if (max_along < static_cast<unsigned>(limit_cap)) {
            max_along = (std::min)(max_along + t, static_cast<unsigned>(limit_cap));
        }
    }
    return out;
}

void SwitcherCore::limits_changed() noexcept {
    const Limits next = compute_limits();
    const bool same = next == limits_;
    limits_ = next;
    if (same || in_create_ || core_wnd() == nullptr) return;
    host_limits_changed();
}

// ---------------------------------------------------------------------------------------------
// Window.

bool SwitcherCore::core_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) noexcept {
    result = 0;
    try {
        switch (msg) {
        case WM_CREATE: on_create(wnd); return true;
        case WM_DESTROY: on_destroy(); return true;
        case WM_SIZE: layout(); return true;
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = static_cast<LONG>(limits_.min_width);
            mmi->ptMinTrackSize.y = static_cast<LONG>(limits_.min_height);
            mmi->ptMaxTrackSize.x = static_cast<LONG>(limits_.max_width);
            mmi->ptMaxTrackSize.y = static_cast<LONG>(limits_.max_height);
            return true;
        }
        // Transparent children paint their background by forwarding these to us with their own
        // DC and origin, so both must really paint. Our own erase only reaches the area no child
        // covers (WS_CLIPCHILDREN).
        case WM_ERASEBKGND: fill_background(reinterpret_cast<HDC>(wp));
            result = 1;
            return true;
        case WM_PRINTCLIENT:
            if ((lp & PRF_ERASEBKGND) != 0) fill_background(reinterpret_cast<HDC>(wp));
            return true;
        case WM_PAINT: {
            // Only reached where no child covers the client area (WS_CLIPCHILDREN).
            PAINTSTRUCT ps{};
            const HDC dc = BeginPaint(wnd, &ps);
            if (dc != nullptr) fill_background(dc);
            EndPaint(wnd, &ps);
            return true;
        }
        case WM_TIMER:
            if (wp == timer_ah_delay || wp == timer_ah_frame) {
                ah_on_timer(wp);
                return true;
            }
            if (wp == timer_drop_switch) {
                on_drop_switch_timer();
                return true;
            }
            break;
        case WM_SETCURSOR:
            // Fallback for a child that raised itself by z-order alone (SetWindowPos(HWND_TOP)),
            // which raises no event at all: the pointer moving over it reaches us here, because
            // DefWindowProc asks the parent first. Never handled, so the cursor is unchanged.
            if (auto_hide() && reinterpret_cast<HWND>(wp) != wnd) ah_raise();
            break;
        case WM_SETFOCUS:
            if (const HWND child = host_child_wnd(); child != nullptr) {
                SetFocus(child);
            } else if (strip_.hwnd() != nullptr && strip_shown_) {
                SetFocus(strip_.hwnd());
            }
            return true;
        default: break;
        }
    } catch (const std::exception& e) {
        log::warn(std::string("container message failed: ") + e.what());
    } catch (...) {
        log::warn("container message failed");
    }
    return false;
}

void SwitcherCore::on_create(HWND wnd) noexcept {
    const bool measure = perf::enabled();
    const std::uint64_t t_start = measure ? perf::now() : 0;
    in_create_ = true;
    attach_shared();
    try {
        playlists::subscribe(*this);
        subscribed_ = true;
        sync_title_needs();
        if (!strip_.create(wnd, *this)) log::warn("could not create the tab strip");
        if (strip_.hwnd() != nullptr && !strip_drop_.attach(strip_.hwnd(), *this)) {
            log::warn("the tab strip takes no drops (RegisterDragDrop failed)");
        }
        strip_.set_settings(settings_);
        update_cover_subscription();
        refresh_colours();
        refresh_font();
        ah_update_mode();
        GetClientRect(wnd, &content_);
        rebuild_strip();
        layout();
    } catch (const std::exception& e) {
        log::warn(std::string("could not set up the element: ") + e.what());
    }
    in_create_ = false;
    limits_ = compute_limits();

    if (measure) {
        const StripWindow::CreateTimings& st = strip_.create_timings();
        char warm[32]{};
        gfx::warm_text_status(warm, sizeof(warm));
        pfc::string_formatter f;
        f << "element created with " << pfc::format_uint(playlists::entries().size()) << " playlists in "
          << pfc::format_float(perf::elapsed_ms(t_start, perf::now()), 0, 3) << " ms (strip: class "
          << pfc::format_float(st.class_ms, 0, 3) << ", window " << pfc::format_float(st.window_ms, 0, 3)
          << ", state " << pfc::format_float(st.state_ms, 0, 3) << ", text " << pfc::format_float(st.text_ms, 0, 3)
          << "; text warm-up " << warm << ")";
        log::info(f.get_ptr());
    }
}

void SwitcherCore::on_destroy() noexcept {
    drop_end();
    strip_drop_.detach();
    KillTimer(core_wnd(), timer_ah_delay);
    KillTimer(core_wnd(), timer_ah_frame);
    ah_timer_ = AhTimer::none;
    ah_animating_ = false;
    ah_shown_ = false;
    ah_progress_ = 0.0f;
    menu_pin_ = false;
    hot_zone_drop_.detach();
    hot_zone_.destroy();
    ah_sync_parent_watch();
    strip_.destroy();
    strip_shown_ = false;
    if (cover_subscribed_) {
        cover::unsubscribe(this);
        cover_subscribed_ = false;
    }
    if (subscribed_) {
        playlists::unsubscribe(*this);
        subscribed_ = false;
    }
    visible_.clear();
    items_.clear();
    labels_.clear();
    child_seen_ = nullptr;
    detach_shared();
}

void SwitcherCore::fill_background(HDC dc) const noexcept {
    if (dc == nullptr) return;
    RECT clip{};
    if (GetClipBox(dc, &clip) == ERROR || IsRectEmpty(&clip)) return;
    // The stock DC brush: no GDI object is created per erase.
    const COLORREF previous = SetDCBrushColor(dc, background_);
    FillRect(dc, &clip, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SetDCBrushColor(dc, previous);
}

void SwitcherCore::refresh_colours() noexcept {
    try {
        const HostColours colours = host_colours();
        const COLORREF panel = colours.background;
        StripTheme theme;
        theme.background = panel;
        theme.text = colours.text;
        theme.dark = colours.dark;
        theme.active_fill = static_cast<float>(settings_.accent_strength) / 100.0f;

        // The strip's own background: the host's (lifted in dark mode by the strip), a custom
        // colour, or the panel with some accent mixed in. Light or dark follows what is drawn.
        std::uint32_t bg = colour::rgb_from_colorref(panel);
        if (settings_.strip_background == StripBackground::custom) {
            bg = settings_.background_argb & 0xFFFFFFu;
            theme.lift = false;
            theme.dark = colour::lightness(bg) < colour::light_background_lightness;
        }
        const auto mix = [](std::uint32_t a, std::uint32_t b, float t) {
            const auto ch = [&](int shift) {
                const float x = static_cast<float>((a >> shift) & 0xFFu);
                const float y = static_cast<float>((b >> shift) & 0xFFu);
                return static_cast<std::uint32_t>(std::lround(x * t + y * (1.0f - t))) << shift;
            };
            return ch(16) | ch(8) | ch(0);
        };

        // Every accent passes a 3:1 contrast floor against the strip. A cover colour is raw, so
        // it also gets the full legibility treatment (lightness window, chroma floor; the same
        // code as Media Bar and foo_onscreendisplay). the host's selection colour and a custom colour are
        // the user's choice and are only nudged when they would vanish.
        std::uint32_t accent = colour::rgb_from_colorref(colours.selection);
        if (settings_.accent_source == AccentSource::custom) accent = settings_.accent_argb & 0xFFFFFFu;
        std::optional<std::uint32_t> cover_raw;
        if (settings_.accent_source == AccentSource::cover) {
            cover_raw = cover::current();
            if (cover_raw) accent = colour::accent_for_background(*cover_raw, bg);
        }
        accent = colour::with_min_contrast(accent, bg, colour::accent_min_contrast);

        if (settings_.strip_background == StripBackground::accent_tint) {
            // Tint what the strip would have shown (dark mode's lift included), then make sure
            // the accent still stands out from its own tint.
            const std::uint32_t text = colour::rgb_from_colorref(theme.text);
            const std::uint32_t base = theme.dark ? mix(text, bg, 0.04f) : bg;
            bg = mix(accent, base, static_cast<float>(settings_.tint_strength) / 100.0f);
            theme.lift = false;
            accent = colour::with_min_contrast(accent, bg, colour::accent_min_contrast);
        }
        if (!theme.lift) {
            theme.text = colour::colorref_from_rgb(
                colour::with_min_contrast(colour::rgb_from_colorref(theme.text), bg, 4.5f));
        }
        theme.background = colour::colorref_from_rgb(bg);
        theme.accent = colour::colorref_from_rgb(accent);
        if (cover_raw && colour::lightness(bg) >= colour::light_background_lightness) {
            // A solid fill on a light strip: the cover's colour in the light window (yellow stays
            // yellow instead of going olive); the strip picks dark text for it.
            theme.fill_accent = colour::colorref_from_rgb(colour::accent_for_card(*cover_raw & 0xFFFFFFu, false));
        }

        background_ = panel;
        hot_zone_.set_colour(panel);
        strip_.set_theme(theme);
        if (const HWND self = core_wnd(); self != nullptr) InvalidateRect(self, nullptr, FALSE);
    } catch (...) {
    }
}

void SwitcherCore::refresh_font() noexcept {
    try {
        StripFont font;
        StripTextOptions options;
        host_font(font, options);

        const int before = strip_.thickness();
        strip_.set_text_options(options);
        strip_.set_font(font);
        if (strip_.thickness() != before && core_wnd() != nullptr) {
            layout();
            limits_changed();
        }
    } catch (...) {
    }
}

void SwitcherCore::on_cover_accent_changed() noexcept { refresh_colours(); }

void SwitcherCore::update_cover_subscription() noexcept {
    const bool want = core_wnd() != nullptr && settings_.accent_source == AccentSource::cover;
    if (want == cover_subscribed_) return;
    cover_subscribed_ = want;
    if (want) {
        cover::subscribe(this);
    } else {
        cover::unsubscribe(this);
    }
}

void SwitcherCore::apply_settings() noexcept {
    clamp(settings_);
    strip_.set_settings(settings_);
    update_cover_subscription();
    refresh_colours();
    ah_update_mode();
    layout();
    limits_changed();
    ah_evaluate();
}


// ---------------------------------------------------------------------------------------------
// Strip intents.

void SwitcherCore::on_strip_activate(std::size_t index) noexcept { activate_playlist(playlist_of_strip(index), true); }

void SwitcherCore::on_strip_step(int direction) noexcept {
    if (visible_.empty()) return;
    const std::size_t current = strip_index_of_playlist(playlists::active());
    std::size_t next = 0;
    if (current == no_index) {
        next = direction > 0 ? 0 : visible_.size() - 1;
    } else if (direction < 0) {
        if (current == 0) return;
        next = current - 1;
    } else {
        if (current + 1 >= visible_.size()) return;
        next = current + 1;
    }
    activate_playlist(visible_[next], true);
}

bool SwitcherCore::on_strip_double_click(std::size_t index) noexcept {
    if (index == no_index) {
        if (!settings_.dblclick_new) return false;
        // At the end, like the built-in Playlist Tabs; named and activated by new_playlist().
        new_playlist(SIZE_MAX);
        return true;
    }
    if (settings_.dblclick_tab != TabDoubleClick::rename) return false;
    const std::size_t playlist = playlist_of_strip(index);
    if (playlist == SIZE_MAX) return false;
    rename_playlist(playlist); // beeps itself for a locked playlist
    return true;
}

void SwitcherCore::on_strip_middle_click(std::size_t index) noexcept {
    const std::size_t playlist = playlist_of_strip(index);
    if (playlist == SIZE_MAX) return;
    switch (settings_.middle_click) {
    case MiddleClick::hide_tab: set_playlist_hidden(playlist, true); break;
    case MiddleClick::remove_playlist:
        if (confirm_remove(playlist)) remove_playlist(playlist);
        break;
    case MiddleClick::nothing:
    default: break;
    }
}

void SwitcherCore::on_strip_reorder(std::size_t from, std::size_t to) noexcept {
    if (from < visible_.size() && to < visible_.size() && from != to) {
        // Next to the tab it was dropped on; hidden playlists in between keep their place.
        move_playlist(visible_[from], visible_[to]);
    } else {
        rebuild_strip();
    }
}

void SwitcherCore::show_tab_menu(std::size_t strip_index, POINT screen, bool full) noexcept {
    const HWND self = core_wnd();
    if (self == nullptr) return;
    const auto keep_alive = host_keep_alive();
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return;
    try {
        const std::vector<std::size_t> snapshot = visible_;
        const std::size_t active = playlists::active();
        const std::size_t clicked = strip_index < snapshot.size() ? snapshot[strip_index] : SIZE_MAX;
        std::vector<std::size_t> hidden;
        bool our_lock = false;
        if (!full) {
            // Chevron: the playlist list only (long lists in groups, append_long_list).
            std::vector<std::wstring> texts;
            texts.reserve(snapshot.size());
            std::size_t checked = SIZE_MAX;
            for (std::size_t s = 0; s < snapshot.size(); ++s) {
                const std::wstring& label = label_of(snapshot[s]);
                texts.push_back(menu_text(label.empty() ? std::wstring(L"(untitled)") : label));
                if (snapshot[s] == active) checked = s;
            }
            append_long_list(menu, texts, menu_tab_base, menu_cmd_base - menu_tab_base, checked);
        } else {
            AppendMenuW(menu, MF_STRING, cmd_new, L"New playlist");
            if (clicked != SIZE_MAX) {
                auto pm = playlist_manager::get();
                const bool side =
                    settings_.position == StripPosition::left || settings_.position == StripPosition::right;
                const UINT first = strip_index == 0 ? MF_GRAYED : 0;
                const UINT last = strip_index + 1 >= snapshot.size() ? MF_GRAYED : 0;
                const UINT no_rename = playlist_allows(clicked, playlist_lock::filter_rename) ? 0 : MF_GRAYED;
                const UINT no_remove =
                    playlist_allows(clicked, playlist_lock::filter_remove_playlist) && playlists::entries().size() > 1
                        ? 0
                        : MF_GRAYED;
                const UINT empty = pm->playlist_get_item_count(clicked) == 0 ? MF_GRAYED : 0;
                AppendMenuW(menu, MF_STRING | no_rename, cmd_rename, L"Rename...");
                AppendMenuW(menu, MF_STRING, cmd_duplicate, L"Duplicate");
                AppendMenuW(menu, MF_STRING | no_remove, cmd_remove, L"Remove");
                AppendMenuW(menu, MF_STRING | empty, cmd_save, L"Save...");
                AppendMenuW(menu, MF_STRING, cmd_load, L"Load playlist...");
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                our_lock = playlists::has_user_lock(clicked);
                if (!our_lock && pm->playlist_lock_is_present(clicked)) {
                    pfc::string8 owner;
                    if (!pm->playlist_lock_query_name(clicked, owner) || owner.is_empty()) owner = "another component";
                    const std::wstring text =
                        L"Locked by " + menu_text(std::wstring(pfc::stringcvt::string_wide_from_utf8(owner))) + L"...";
                    AppendMenuW(menu, MF_STRING, cmd_lock_ui, text.c_str());
                } else {
                    AppendMenuW(menu, MF_STRING | (our_lock ? MF_CHECKED : 0), cmd_lock, L"Lock playlist");
                }
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING | (snapshot.size() < 2 ? MF_GRAYED : 0), cmd_hide, L"Hide tab");
                AppendMenuW(menu, MF_STRING | first, cmd_move_back, side ? L"Move up" : L"Move left");
                AppendMenuW(menu, MF_STRING | last, cmd_move_forward, side ? L"Move down" : L"Move right");
            }
            if (clicked == SIZE_MAX) AppendMenuW(menu, MF_STRING, cmd_load, L"Load playlist...");
            if (clicked == SIZE_MAX && perf::enabled()) {
                // Only with the performance log on: a quick way to measure with many playlists.
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, cmd_perf_create, L"Perf: create 500 test playlists");
                AppendMenuW(menu, MF_STRING, cmd_perf_remove, L"Perf: remove the test playlists");
            }
            const auto& entries = playlists::entries();
            for (std::size_t i = 0; i < entries.size(); ++i) {
                if (entries[i].hidden()) hidden.push_back(i);
            }
            if (!hidden.empty()) {
                if (HMENU sub = CreatePopupMenu(); sub != nullptr) {
                    std::vector<std::wstring> texts;
                    texts.reserve(hidden.size());
                    for (const std::size_t p : hidden) {
                        texts.push_back(menu_text(!label_of(p).empty() ? label_of(p) : entries[p].name));
                    }
                    append_long_list(sub, texts, menu_unhide_base, menu_style_base - menu_unhide_base, SIZE_MAX);
                    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(sub), L"Show hidden tab");
                }
            }
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            append_style_menu(menu);
            AppendMenuW(menu, MF_STRING, cmd_configure, L"Configure...");
        }
        const UINT cmd = static_cast<UINT>(TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD,
                                                          screen.x, screen.y, 0, self, nullptr));
        DestroyMenu(menu);
        menu = nullptr;
        // The playlists may have changed while the menu was open: act only if they did not.
        const bool same = snapshot == visible_;
        if (cmd >= menu_style_base && cmd < style_last) {
            run_style_command(cmd);
        } else if (cmd >= menu_unhide_base && cmd < menu_style_base) {
            const std::size_t h = cmd - menu_unhide_base;
            if (h < hidden.size() && same) set_playlist_hidden(hidden[h], false);
        } else if (cmd >= menu_cmd_base && cmd < menu_unhide_base) {
            const bool valid = clicked != SIZE_MAX && same;
            switch (cmd) {
            case cmd_new: new_playlist(valid ? clicked + 1 : SIZE_MAX); break;
            case cmd_rename:
                if (valid) rename_playlist(clicked);
                break;
            case cmd_duplicate:
                if (valid) duplicate_playlist(clicked);
                break;
            case cmd_remove:
                if (valid) remove_playlist(clicked);
                break;
            case cmd_save:
                if (valid) save_playlist(clicked);
                break;
            case cmd_load: load_playlist(); break;
            case cmd_perf_create: perf_test_playlists(true); break;
            case cmd_perf_remove: perf_test_playlists(false); break;
            case cmd_lock:
                if (valid && !playlists::set_user_lock(clicked, !our_lock)) MessageBeep(MB_ICONWARNING);
                break;
            case cmd_lock_ui:
                if (valid) playlist_manager::get()->playlist_lock_show_ui(clicked);
                break;
            case cmd_hide:
                if (valid) set_playlist_hidden(clicked, true);
                break;
            case cmd_move_back:
                if (valid && strip_index > 0) move_playlist(snapshot[strip_index], snapshot[strip_index - 1]);
                break;
            case cmd_move_forward:
                if (valid && strip_index + 1 < snapshot.size()) {
                    move_playlist(snapshot[strip_index], snapshot[strip_index + 1]);
                }
                break;
            case cmd_configure: run_configure(self); break;
            default: break;
            }
        } else if (cmd >= menu_tab_base && cmd < menu_cmd_base) {
            const std::size_t s = cmd - menu_tab_base;
            if (s < snapshot.size() && same) activate_playlist(snapshot[s], true);
        }
    } catch (const std::exception& e) {
        log::warn(std::string("tab menu failed: ") + e.what());
    }
    if (menu != nullptr) DestroyMenu(menu);
}

void SwitcherCore::duplicate_playlist(std::size_t playlist) noexcept {
    try {
        auto pm = playlist_manager::get();
        if (playlist >= pm->get_playlist_count()) return;
        pfc::string8 name;
        pm->playlist_get_name(playlist, name);
        name << " (copy)";
        metadb_handle_list items;
        pm->playlist_get_all_items(playlist, items);
        const std::size_t created = pm->create_playlist(name, SIZE_MAX, playlist + 1);
        if (created == SIZE_MAX) return;
        pm->playlist_add_items(created, items, bit_array_false());
        activate_playlist(created, true);
    } catch (const std::exception& e) {
        log::warn(std::string("duplicate playlist failed: ") + e.what());
    }
}

namespace {

struct SaveFormat {
    std::wstring extension; // "fpl"
};

//! The writable playlist formats, foobar2000's own .fpl first.
std::vector<SaveFormat> save_formats() {
    std::vector<SaveFormat> out;
    for (auto l : playlist_loader::enumerate()) {
        if (!l->can_write()) continue;
        const char* ext = l->get_extension();
        if (ext == nullptr || *ext == 0) continue;
        std::wstring w{pfc::stringcvt::string_wide_from_utf8(ext).get_ptr()};
        bool dup = false;
        for (const auto& f : out) dup = dup || _wcsicmp(f.extension.c_str(), w.c_str()) == 0;
        if (!dup) out.push_back({std::move(w)});
    }
    std::stable_partition(out.begin(), out.end(),
                          [](const SaveFormat& f) { return _wcsicmp(f.extension.c_str(), L"fpl") == 0; });
    return out;
}

} // namespace

void SwitcherCore::save_playlist(std::size_t playlist) noexcept {
    const auto keep_alive = host_keep_alive();
    try {
        auto pm = playlist_manager::get();
        if (playlist >= pm->get_playlist_count()) return;
        metadb_handle_list items;
        pm->playlist_get_all_items(playlist, items);
        if (items.get_count() == 0) return;
        const std::vector<SaveFormat> formats = save_formats();
        if (formats.empty()) {
            // No writer found: fall back to foobar2000's own dialog.
            standard_commands::context_save_playlist(items);
            return;
        }
        pfc::string8 name;
        pm->playlist_get_name(playlist, name);
        // Filter "FPL playlist (*.fpl)\0*.fpl\0...", embedded NULs, double NUL at the end.
        std::wstring filter;
        for (const auto& f : formats) {
            std::wstring upper = f.extension;
            for (auto& c : upper) c = static_cast<wchar_t>(towupper(c));
            filter += upper + L" playlist (*." + f.extension + L")";
            filter.push_back(0);
            filter += L"*." + f.extension;
            filter.push_back(0);
        }
        filter.push_back(0);
        std::wstring file = safe_file_name(std::wstring(pfc::stringcvt::string_wide_from_utf8(name)));
        file.resize(MAX_PATH * 4, 0);
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = core_api::get_main_window();
        ofn.lpstrFilter = filter.c_str();
        ofn.nFilterIndex = 1;
        ofn.lpstrFile = file.data();
        ofn.nMaxFile = static_cast<DWORD>(file.size());
        ofn.lpstrDefExt = formats.front().extension.c_str();
        ofn.lpstrTitle = L"Save playlist";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
        menu_pin_ = true;
        const BOOL ok = GetSaveFileNameW(&ofn);
        menu_pin_ = false;
        ah_evaluate();
        if (!ok) return;
        file.resize(wcslen(file.c_str()));
        // The extension typed wins; with none (or an unknown one), the chosen filter's.
        const std::size_t pick = ofn.nFilterIndex >= 1 && ofn.nFilterIndex <= formats.size() ? ofn.nFilterIndex - 1 : 0;
        const std::size_t dot = file.find_last_of(L'.');
        const std::size_t slash = file.find_last_of(L"\\/");
        bool known = false;
        if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash)) {
            const std::wstring ext = file.substr(dot + 1);
            for (const auto& f : formats) known = known || _wcsicmp(f.extension.c_str(), ext.c_str()) == 0;
        }
        if (!known) file += L"." + formats[pick].extension;
        const pfc::string8 path(pfc::stringcvt::string_utf8_from_wide(file.c_str()));
        try {
            playlist_loader::g_save_playlist(path, items, fb2k::noAbort);
        } catch (const std::exception& e) {
            pfc::string8 message("Could not save the playlist:\n");
            message << path << "\n\n" << e.what();
            popup_message::g_show(message, "Enhanced Playlist Tabs");
        }
    } catch (const std::exception& e) {
        menu_pin_ = false;
        log::warn(std::string("save playlist failed: ") + e.what());
    }
}

void SwitcherCore::perf_test_playlists(bool create) noexcept {
    static constexpr char prefix[] = "EPT perf ";
    try {
        auto pm = playlist_manager::get();
        const std::uint64_t t_start = perf::now();
        std::size_t done = 0;
        // One summary line instead of 500 event lines (the console keeps only so many).
        g_perf_bulk = true;
        if (create) {
            for (unsigned i = 1; i <= 500; ++i) {
                pfc::string8 name(prefix);
                name << pfc::format_uint(i, 3);
                if (pm->create_playlist(name, SIZE_MAX, SIZE_MAX) != SIZE_MAX) ++done;
            }
        } else {
            const std::size_t count = pm->get_playlist_count();
            pfc::bit_array_bittable mask(count);
            pfc::string8 name;
            for (std::size_t i = 0; i < count; ++i) {
                if (pm->playlist_get_name(i, name) && pfc::strcmp_partial(name, prefix) == 0) {
                    mask.set(i, true);
                    ++done;
                }
            }
            if (done != 0) pm->remove_playlists(mask);
        }
        g_perf_bulk = false;
        pfc::string_formatter f;
        f << "perf: " << (create ? "created " : "removed ") << pfc::format_uint(done) << " test playlists in "
          << pfc::format_float(perf::elapsed_ms(t_start, perf::now()), 0, 1) << " ms (all events included)";
        log::info(f.get_ptr());
    } catch (const std::exception& e) {
        g_perf_bulk = false;
        log::warn(std::string("perf test playlists failed: ") + e.what());
    }
}

void SwitcherCore::load_playlist() noexcept {
    const auto keep_alive = host_keep_alive();
    try {
        menu_pin_ = true;
        // File > Load playlist...: foobar2000's dialog, loading into a new playlist.
        standard_commands::main_load_playlist();
        menu_pin_ = false;
        ah_evaluate();
    } catch (...) {
        menu_pin_ = false;
    }
}

bool SwitcherCore::confirm_remove(std::size_t playlist) noexcept {
    try {
        auto pm = playlist_manager::get();
        if (playlist >= pm->get_playlist_count()) return false;
        const std::size_t count = pm->playlist_get_item_count(playlist);
        if (!settings_.confirm_remove || count == 0) return true;
        const std::uint64_t key = playlists::entries()[playlist].key;
        pfc::string8 name;
        pm->playlist_get_name(playlist, name);
        std::wstring text = L"Remove the playlist \"" + std::wstring(pfc::stringcvt::string_wide_from_utf8(name)) + L"\"";
        text += L" (" + std::to_wstring(count) + (count == 1 ? L" track)" : L" tracks)");
        text += L"?";
        const auto keep_alive = host_keep_alive();
        menu_pin_ = true;
        const bool yes = run_confirm_dialog(core_api::get_main_window(), L"Remove playlist", text);
        menu_pin_ = false;
        ah_evaluate();
        // The playlist may have moved while the box was open: it must still be this one.
        return yes && playlists::index_of_key(key) == playlist;
    } catch (...) {
        menu_pin_ = false;
        return false;
    }
}

void SwitcherCore::on_strip_menu(std::size_t index, POINT screen) noexcept {
    if (host_strip_menu(index, screen)) return;
    // Pins an auto-hidden strip for the menu and any dialog it opens.
    menu_pin_ = true;
    show_tab_menu(index, screen, true);
    menu_pin_ = false;
    ah_evaluate();
}

void SwitcherCore::on_strip_overflow(POINT screen) noexcept {
    menu_pin_ = true;
    show_tab_menu(no_index, screen, false);
    menu_pin_ = false;
    ah_evaluate();
}

void SwitcherCore::append_style_menu(HMENU menu) const noexcept {
    HMENU style = CreatePopupMenu();
    if (style == nullptr) return;
    const auto radio = [](HMENU m, unsigned id, const wchar_t* text, bool on) {
        AppendMenuW(m, MF_STRING | (on ? MF_CHECKED : 0), id, text);
        if (on) {
            MENUITEMINFOW mii{sizeof(mii)};
            mii.fMask = MIIM_FTYPE;
            mii.fType = MFT_STRING | MFT_RADIOCHECK;
            SetMenuItemInfoW(m, id, FALSE, &mii);
        }
    };
    const auto sub = [&](const wchar_t* text) {
        HMENU m = CreatePopupMenu();
        if (m != nullptr) AppendMenuW(style, MF_POPUP, reinterpret_cast<UINT_PTR>(m), text);
        return m;
    };
    const Settings& s = settings_;
    if (HMENU m = sub(L"Strip position"); m != nullptr) {
        radio(m, style_position_top, L"Top", s.position == StripPosition::top);
        radio(m, style_position_bottom, L"Bottom", s.position == StripPosition::bottom);
        radio(m, style_position_left, L"Left", s.position == StripPosition::left);
        radio(m, style_position_right, L"Right", s.position == StripPosition::right);
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        const bool side = s.position == StripPosition::left || s.position == StripPosition::right;
        AppendMenuW(m, MF_STRING | (s.side_text == SideText::rotated ? MF_CHECKED : 0) | (side ? 0 : MF_GRAYED),
                    style_rotate_side_text, L"Rotate text on side strips");
    }
    if (HMENU m = sub(L"Active tab"); m != nullptr) {
        radio(m, style_indicator_underline, L"Underline", s.indicator == Indicator::underline);
        radio(m, style_indicator_pill, L"Pill", s.indicator == Indicator::pill);
        radio(m, style_indicator_none, L"Text only", s.indicator == Indicator::none);
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING | (s.chip ? MF_CHECKED : 0), style_chip, L"Chips");
    }
    if (HMENU m = sub(L"Accent colour"); m != nullptr) {
        const std::wstring text = std::wstring(L"Default UI selection colour");
        radio(m, style_accent_selection, text.c_str(), s.accent_source == AccentSource::selection);
        radio(m, style_accent_cover, L"From the playing cover", s.accent_source == AccentSource::cover);
        radio(m, style_accent_custom, L"Custom...", s.accent_source == AccentSource::custom);
    }
    if (HMENU m = sub(L"Accent strength"); m != nullptr) {
        // Opacity of the active tab's fill; the underline is always solid.
        const UINT grey = s.indicator == Indicator::pill || s.chip ? 0 : MF_GRAYED;
        const auto level = [&](unsigned id, const wchar_t* text, bool on) {
            radio(m, id, text, on);
            if (grey != 0) EnableMenuItem(m, id, MF_BYCOMMAND | MF_GRAYED);
        };
        const std::uint8_t a = s.accent_strength;
        level(style_strength_auto, L"Automatic", a == 0);
        level(style_strength_subtle, L"Subtle (15%)", a == 15);
        level(style_strength_medium, L"Medium (35%)", a == 35);
        level(style_strength_strong, L"Strong (60%)", a == 60);
        level(style_strength_solid, L"Solid", a == 100);
    }
    if (HMENU m = sub(L"Strip background"); m != nullptr) {
        const std::wstring text = std::wstring(L"Default UI background");
        radio(m, style_background_theme, text.c_str(), s.strip_background == StripBackground::theme);
        radio(m, style_background_tint, L"Tinted with the accent", s.strip_background == StripBackground::accent_tint);
        radio(m, style_background_custom, L"Custom...", s.strip_background == StripBackground::custom);
    }
    if (HMENU m = sub(L"Tab width"); m != nullptr) {
        radio(m, style_sizing_fit, L"Fit the title", s.sizing == TabSizing::fit);
        radio(m, style_sizing_equal, L"All equal", s.sizing == TabSizing::equal);
        radio(m, style_sizing_fill, L"Fill the strip", s.sizing == TabSizing::fill);
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        const bool slack = s.sizing != TabSizing::fill;
        const UINT grey = slack ? 0 : MF_GRAYED;
        AppendMenuW(m, MF_STRING | grey | (s.align == TabAlign::start ? MF_CHECKED : 0), style_align_start,
                    L"Align to start");
        AppendMenuW(m, MF_STRING | grey | (s.align == TabAlign::centre ? MF_CHECKED : 0), style_align_centre,
                    L"Centre");
        AppendMenuW(m, MF_STRING | grey | (s.align == TabAlign::end ? MF_CHECKED : 0), style_align_end,
                    L"Align to end");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING | (s.chevron_position == ChevronPosition::start ? MF_CHECKED : 0),
                    style_chevron_start, L"Overflow chevron at the start");
        AppendMenuW(m, MF_STRING | (s.shrink_titles ? MF_CHECKED : 0), style_shrink_titles,
                    L"Shorten titles before showing the chevron");
    }
    if (HMENU m = sub(L"Show strip"); m != nullptr) {
        radio(m, style_show_always, L"Always", s.visibility == StripVisibility::always);
        radio(m, style_show_two_or_more, L"Only with two or more playlists",
              s.visibility == StripVisibility::two_or_more);
        radio(m, style_show_auto_hide, L"Auto-hide", s.visibility == StripVisibility::auto_hide);
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(style), L"Appearance");
}

void SwitcherCore::run_style_command(unsigned command) noexcept {
    Settings& s = settings_;
    switch (command) {
    case style_position_top: s.position = StripPosition::top; break;
    case style_position_bottom: s.position = StripPosition::bottom; break;
    case style_position_left: s.position = StripPosition::left; break;
    case style_position_right: s.position = StripPosition::right; break;
    case style_rotate_side_text:
        s.side_text = s.side_text == SideText::rotated ? SideText::horizontal : SideText::rotated;
        break;
    case style_indicator_underline: s.indicator = Indicator::underline; break;
    case style_indicator_pill: s.indicator = Indicator::pill; break;
    case style_indicator_none: s.indicator = Indicator::none; break;
    case style_chip: s.chip = !s.chip; break;
    case style_accent_selection: s.accent_source = AccentSource::selection; break;
    case style_accent_cover: s.accent_source = AccentSource::cover; break;
    case style_accent_custom: {
        if (!pick_colour(s.accent_argb)) return;
        s.accent_source = AccentSource::custom;
        break;
    }
    case style_strength_auto: s.accent_strength = 0; break;
    case style_strength_subtle: s.accent_strength = 15; break;
    case style_strength_medium: s.accent_strength = 35; break;
    case style_strength_strong: s.accent_strength = 60; break;
    case style_strength_solid: s.accent_strength = 100; break;
    case style_background_theme: s.strip_background = StripBackground::theme; break;
    case style_background_tint: s.strip_background = StripBackground::accent_tint; break;
    case style_background_custom: {
        if (!pick_colour(s.background_argb)) return;
        s.strip_background = StripBackground::custom;
        break;
    }
    case style_sizing_fit: s.sizing = TabSizing::fit; break;
    case style_sizing_equal: s.sizing = TabSizing::equal; break;
    case style_sizing_fill: s.sizing = TabSizing::fill; break;
    case style_align_start: s.align = TabAlign::start; break;
    case style_align_centre: s.align = TabAlign::centre; break;
    case style_align_end: s.align = TabAlign::end; break;
    case style_chevron_start:
        s.chevron_position =
            s.chevron_position == ChevronPosition::start ? ChevronPosition::end : ChevronPosition::start;
        break;
    case style_shrink_titles: s.shrink_titles = !s.shrink_titles; break;
    case style_show_always: s.visibility = StripVisibility::always; break;
    case style_show_two_or_more: s.visibility = StripVisibility::two_or_more; break;
    case style_show_auto_hide: s.visibility = StripVisibility::auto_hide; break;
    default: return;
    }
    apply_settings();
}
bool SwitcherCore::run_configure(HWND parent) {
    const auto keep_alive = host_keep_alive();
    ConfigureState original;
    original.settings = settings_;
    ConfigureState state = original;
    bool ok = false;
    menu_pin_ = true;
    try {
        ok = run_configure_dialog(parent != nullptr ? parent : core_api::get_main_window(), state, *this,
                                  core_wnd() != nullptr);
    } catch (const std::exception& e) {
        log::warn(std::string("the Configure dialog failed: ") + e.what());
    }
    menu_pin_ = false;
    // Cancel puts back what the live preview changed.
    preview(ok ? state : original);
    return ok;
}

void SwitcherCore::preview(const ConfigureState& state) noexcept {
    try {
        const bool titles_changed =
            state.settings.title_mode != settings_.title_mode || state.settings.title_format != settings_.title_format;
        settings_ = state.settings;
        clamp(settings_);
        if (core_wnd() == nullptr) return;
        strip_.set_settings(settings_);
        update_cover_subscription();
        refresh_colours();
        ah_update_mode();
        if (titles_changed) {
            title_script_.release();
            title_source_.clear();
            sync_title_needs();
        }
        rebuild_strip();
        layout();
        limits_changed();
        ah_evaluate();
    } catch (const std::exception& e) {
        log::warn(std::string("could not apply the settings: ") + e.what());
    }
}

bool SwitcherCore::on_strip_key(UINT message, WPARAM key) noexcept {
    try {
        if (message == WM_KEYDOWN && key == VK_TAB) {
            host_tab_key(strip_.hwnd());
            return true;
        }
        return host_shortcut(key);
    } catch (...) {
        return false;
    }
}

void SwitcherCore::on_strip_metrics_changed() noexcept {
    // A DPI change usually means another monitor: its text rendering parameters differ.
    refresh_font();
    layout();
    limits_changed();
}

// ---------------------------------------------------------------------------------------------
// Auto-hide. Event driven: the hot zone and the strip report the pointer
// (TrackMouseEvent), menus, drags and focus pin it. One delay timer and, while a show/hide
// animation runs, one frame timer. Nothing runs while idle.

int SwitcherCore::ah_px(unsigned dip) const noexcept {
    const unsigned dpi = core_wnd() != nullptr ? gfx::window_dpi(core_wnd()) : gfx::system_dpi();
    return MulDiv(static_cast<int>(dip), static_cast<int>(dpi), 96);
}

void SwitcherCore::ah_update_mode() noexcept {
    const HWND self = core_wnd();
    if (self == nullptr || strip_.hwnd() == nullptr || !auto_hide()) {
        if (self != nullptr) {
            KillTimer(self, timer_ah_delay);
            KillTimer(self, timer_ah_frame);
        }
        ah_timer_ = AhTimer::none;
        ah_animating_ = false;
        ah_shown_ = false;
        ah_progress_ = 0.0f;
        hot_zone_drop_.detach();
        hot_zone_.destroy();
        ah_sync_parent_watch();
        (void)strip_.set_layered(false);
        return;
    }
    // Child layered windows are Windows 8+. Ask for the invisible hot zone there; if Windows
    // grants it, the strip can be layered too and go over the panel.
    if (hot_zone_.hwnd() == nullptr) (void)hot_zone_.create(self, *this, gfx::layered_children_supported());
    sync_hot_zone_drop();
    ah_sync_parent_watch();
    hot_zone_.set_colour(background_);
    const bool want_layered = settings_.reveal_mode == RevealMode::overlay && hot_zone_.layered();
    if (!strip_.set_layered(want_layered) && want_layered) log::warn("auto-hide: the strip could not be layered; pushing the panel instead");
    if (settings_.show_hide_animation == ShowHideAnimation::none || !ah_overlay()) {
        KillTimer(self, timer_ah_frame);
        ah_animating_ = false;
        ah_progress_ = ah_shown_ ? 1.0f : 0.0f;
    }
}

bool SwitcherCore::ah_covered() const noexcept {
    const HWND self = core_wnd();
    if (self == nullptr) return false;
    const HWND strip = strip_.hwnd();
    const HWND zone = hot_zone_.hwnd();
    int pending = (strip != nullptr ? 1 : 0) + (zone != nullptr ? 1 : 0);
    // Ours must be the first children in z-order. Hidden ones count: a hot zone left under a
    // panel while the strip is shown would be just as dead once it is shown again.
    for (HWND wnd = GetWindow(self, GW_CHILD); wnd != nullptr && pending > 0; wnd = GetWindow(wnd, GW_HWNDNEXT)) {
        if (wnd != strip && wnd != zone) return true;
        --pending;
    }
    return false;
}

void SwitcherCore::ah_raise() noexcept {
    if (!ah_covered()) return;
    // Hot zone, then strip, so the strip ends up first.
    for (const HWND wnd : {hot_zone_.hwnd(), strip_.hwnd()}) {
        if (wnd == nullptr) continue;
        SetWindowPos(wnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
}

// SetParent puts the window on top of its new siblings, and nothing tells the parent: no
// message, no EVENT_OBJECT_REORDER (test/zorder_test.cpp). Visualisations with their own
// fullscreen mode may reparent their window to a monitor-sized popup and back, which left it
// over the hot zone, so the strip could no longer be revealed. EVENT_OBJECT_PARENTCHANGE does report it. One out-of-context hook for
// the process, only while some container has a hot zone; the callback runs from our message
// loop after the reparenting call has returned.
namespace {
HWINEVENTHOOK g_parent_watch = nullptr;
#ifndef EVENT_OBJECT_PARENTCHANGE
constexpr DWORD EVENT_OBJECT_PARENTCHANGE = 0x800F;
#endif
} // namespace

void SwitcherCore::ah_sync_parent_watch() noexcept {
    bool wanted = false;
    for (const SwitcherCore* core : live()) wanted = wanted || core->hot_zone_.hwnd() != nullptr;
    if (wanted && g_parent_watch == nullptr) {
        g_parent_watch = SetWinEventHook(EVENT_OBJECT_PARENTCHANGE, EVENT_OBJECT_PARENTCHANGE, nullptr,
                                         &SwitcherCore::ah_on_parent_change, GetCurrentProcessId(), 0,
                                         WINEVENT_OUTOFCONTEXT);
        if (g_parent_watch == nullptr) log::warn("auto-hide: could not watch for panels being reparented");
    } else if (!wanted && g_parent_watch != nullptr) {
        UnhookWinEvent(g_parent_watch);
        g_parent_watch = nullptr;
    }
}

void CALLBACK SwitcherCore::ah_on_parent_change(HWINEVENTHOOK, DWORD event, HWND wnd, LONG object, LONG, DWORD,
                                            DWORD) noexcept {
    if (event != EVENT_OBJECT_PARENTCHANGE || object != OBJID_WINDOW || wnd == nullptr) return;
    const HWND parent = GetAncestor(wnd, GA_PARENT);
    if (parent == nullptr) return;
    for (SwitcherCore* core : live()) {
        if (core->core_wnd() == parent && core->hot_zone_.hwnd() != nullptr) core->ah_raise();
    }
}

bool SwitcherCore::ah_pointer_or_pinned() const noexcept {
    const HWND strip = strip_.hwnd();
    if (menu_pin_ || strip_.dragging() || drop_hovering_) return true;
    if (strip != nullptr && (GetCapture() == strip || GetFocus() == strip)) return true;
    POINT pt{};
    if (!GetCursorPos(&pt)) return false;
    for (const HWND wnd : {strip, hot_zone_.hwnd()}) {
        if (wnd == nullptr || !IsWindowVisible(wnd)) continue;
        RECT rc{};
        if (GetWindowRect(wnd, &rc) && PtInRect(&rc, pt)) return true;
    }
    return false;
}

void SwitcherCore::ah_set_timer(AhTimer kind, unsigned ms) noexcept {
    const HWND self = core_wnd();
    ah_timer_ = kind;
    if (self == nullptr) return;
    if (kind == AhTimer::none) {
        KillTimer(self, timer_ah_delay);
    } else {
        SetTimer(self, timer_ah_delay, (std::max)(ms, static_cast<unsigned>(USER_TIMER_MINIMUM)), nullptr);
    }
}

void SwitcherCore::ah_evaluate() noexcept {
    if (!auto_hide() || core_wnd() == nullptr || strip_.hwnd() == nullptr) return;
    if (ah_pointer_or_pinned()) {
        if (ah_shown_) {
            if (ah_timer_ == AhTimer::hide) ah_set_timer(AhTimer::none, 0);
            return;
        }
        if (settings_.reveal_delay_ms == 0) {
            ah_set_timer(AhTimer::none, 0);
            ah_set_shown(true);
        } else if (ah_timer_ != AhTimer::reveal) {
            ah_set_timer(AhTimer::reveal, settings_.reveal_delay_ms);
        }
        return;
    }
    if (!ah_shown_) {
        if (ah_timer_ == AhTimer::reveal) ah_set_timer(AhTimer::none, 0);
        return;
    }
    if (ah_timer_ == AhTimer::hide) return;
    unsigned delay = settings_.hide_delay_ms;
    const ULONGLONG now = GetTickCount64();
    if (linger_until_ > now) delay = (std::max)(delay, static_cast<unsigned>(linger_until_ - now));
    if (delay == 0) {
        ah_set_timer(AhTimer::none, 0);
        ah_set_shown(false);
    } else {
        ah_set_timer(AhTimer::hide, delay);
    }
}

void SwitcherCore::ah_set_shown(bool shown) noexcept {
    const HWND self = core_wnd();
    if (self == nullptr || shown == ah_shown_) return;
    ah_shown_ = shown;
    const bool animate = ah_overlay() && settings_.show_hide_animation != ShowHideAnimation::none;
    if (animate) {
        ah_from_ = ah_progress_;
        ah_anim_start_ = GetTickCount64();
        ah_animating_ = true;
        SetTimer(self, timer_ah_frame, USER_TIMER_MINIMUM, nullptr);
    } else {
        KillTimer(self, timer_ah_frame);
        ah_animating_ = false;
        ah_progress_ = shown ? 1.0f : 0.0f;
    }
    layout();
    if (shown) strip_.track_pointer();
}

void SwitcherCore::ah_on_timer(UINT_PTR id) noexcept {
    const HWND self = core_wnd();
    if (self == nullptr) return;
    if (id == timer_ah_delay) {
        const AhTimer kind = ah_timer_;
        ah_set_timer(AhTimer::none, 0);
        const bool want = ah_pointer_or_pinned();
        if (kind == AhTimer::reveal && want) ah_set_shown(true);
        if (kind == AhTimer::hide && !want) ah_set_shown(false);
        return;
    }
    if (id != timer_ah_frame) return;
    const float target = ah_shown_ ? 1.0f : 0.0f;
    // A reversal mid-way takes only the remaining share of the duration.
    const float span = (std::max)(0.05f, std::fabs(target - ah_from_));
    const float duration = static_cast<float>(settings_.animation_ms) * span;
    const float t = std::clamp(static_cast<float>(GetTickCount64() - ah_anim_start_) / duration, 0.0f, 1.0f);
    const float eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); // ease-out cubic
    ah_progress_ = ah_from_ + (target - ah_from_) * eased;
    if (t >= 1.0f) {
        ah_progress_ = target;
        ah_animating_ = false;
        KillTimer(self, timer_ah_frame);
    }
    layout();
    // A slide ends under the pointer: arm the leave tracking again.
    if (!ah_animating_ && ah_shown_) strip_.track_pointer();
}

void SwitcherCore::ah_note_switch() noexcept {
    if (!auto_hide() || core_wnd() == nullptr) return;
    linger_until_ = GetTickCount64() + settings_.linger_ms;
    if (!ah_shown_ && settings_.linger_ms != 0) {
        ah_set_timer(AhTimer::none, 0);
        ah_set_shown(true);
    }
    ah_evaluate();
}

void SwitcherCore::on_strip_pointer() noexcept { ah_evaluate(); }

void SwitcherCore::on_hot_zone(bool, bool clicked) noexcept {
    if (clicked && auto_hide() && !ah_shown_) {
        // A click in the hot zone reveals at once, whatever the delay.
        ah_set_timer(AhTimer::none, 0);
        ah_set_shown(true);
        return;
    }
    ah_evaluate();
}


} // namespace ept
