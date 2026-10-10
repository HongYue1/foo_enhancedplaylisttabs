#pragma once

// The playlist switcher behind the Default UI element (dui_element.cpp) and the Columns UI
// container (cui_container.cpp): the strip, one tab per playlist, switching, layout of the strip
// and the single hosted child, auto-hide, colours, menus and the Configure dialog. Derived from
// Better Tabs' TabsCore; each host supplies the child window and its UI's colours and fonts
// through the host_* hooks.
//
// Performance shape:
//  - a switch is playlist_manager::set_active_playlist plus StripWindow::set_active (two tab
//    rectangles invalidated). The child is not moved, resized or invalidated by us;
//  - playlist changes arrive once per process from playlists::Model and rebuild only the strip
//    items (text layouts are reused for unchanged titles);
//  - a resize moves the strip and the child only;
//  - no timers, hooks or polling while idle.

#include <helpers/foobar2000+atl.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../model/codec.h"
#include "../model/playlist_sort.h"
#include "../model/settings.h"
#include "../platform/cover_hub.h"
#include "../playlists/playlist_model.h"
#include "../strip/hot_zone.h"
#include "../strip/strip_window.h"
#include "configure_dialog.h"
#include "panel_limits.h"
#include "strip_drop.h"

namespace ept {


enum class PlaybackEvent : std::uint8_t { started, stopped, title };

//! The host's own colours, before our settings are applied.
struct HostColours {
    COLORREF background{RGB(255, 255, 255)};
    COLORREF text{RGB(0, 0, 0)};
    COLORREF selection{RGB(0, 120, 215)};
    //! Default UI: the highlight colour. Columns UI: the active item frame.
    COLORREF highlight{RGB(0, 120, 215)};
    bool dark{false};
    //! Behind transparent children (a splitter's dividers), so they look as they do elsewhere in
    //! the host's layout. Unset: `background`.
    std::optional<COLORREF> layout;
};

class SwitcherCore : protected StripListener,
                     protected HotZoneListener,
                     protected cover::Listener,
                     protected ConfigureTarget,
                     protected playlists::Listener,
                     protected DropHandler {
public:
    SwitcherCore();
    virtual ~SwitcherCore();
    SwitcherCore(const SwitcherCore&) = delete;
    SwitcherCore& operator=(const SwitcherCore&) = delete;

    //! Every element that exists. Main thread.
    [[nodiscard]] static const std::vector<SwitcherCore*>& live() noexcept;

    [[nodiscard]] virtual HWND core_wnd() const noexcept = 0;
    //! From the process-wide playback watch: follow the playing playlist, refresh titles.
    void on_playback(PlaybackEvent event) noexcept;
    //! The playback queue changed (coalesced, main thread).
    void on_queue() noexcept;
    //! Ctrl+Tab: the next (+1) or previous (-1) visible playlist, wrapping. False when this
    //! element does not take it (switched off, fewer than two tabs).
    bool cycle_tabs(int direction) noexcept;
    //! `fade`: animate to the new colours (Settings::animations permitting).
    void refresh_colours(bool fade = false) noexcept;
    void refresh_font() noexcept;

protected:
    // Host hooks ---------------------------------------------------------------------------

    //! The hosted element's window, or null (none yet, or it failed).
    [[nodiscard]] virtual HWND host_child_wnd() const noexcept = 0;
    //! The child's size limits; the default asks its window (WM_GETMINMAXINFO).
    virtual void host_query_limits(Limits& out) noexcept;
    //! Our size limits changed: tell the parent.
    virtual void host_limits_changed() noexcept = 0;
    //! The element is shown (by the host's rules, not only IsWindowVisible).
    [[nodiscard]] virtual bool host_visible() const noexcept = 0;
    [[nodiscard]] virtual HostColours host_colours() const noexcept = 0;
    virtual void host_font(StripFont& font, StripTextOptions& options) const noexcept = 0;
    //! Right click on the strip: true if the host takes it (layout editing).
    virtual bool host_strip_menu(std::size_t, POINT) noexcept { return false; }
    //! Tab on the focused strip: move the focus on.
    virtual void host_tab_key(HWND from) noexcept = 0;
    //! A key the strip does not use: run foobar2000's keyboard shortcuts. True if one ran.
    virtual bool host_shortcut(WPARAM key) noexcept = 0;
    //! Holds a reference on the element while a menu or dialog runs.
    [[nodiscard]] virtual service_ptr_t<service_base> host_keep_alive() noexcept = 0;
    //! For labels such as "Default UI background".
    [[nodiscard]] virtual const wchar_t* host_ui_name() const noexcept { return L"Default UI"; }
    //! The UI's own name for HostColours::highlight, after host_ui_name().
    [[nodiscard]] virtual const wchar_t* host_highlight_name() const noexcept { return L"highlight colour"; }

    // Configuration --------------------------------------------------------------------------

    void load_settings(Settings settings, std::vector<RawField> unknown_settings);
    //! Replaces the settings, live if the window exists.
    void reload_settings(Settings settings, std::vector<RawField> unknown_settings);

    // The child ------------------------------------------------------------------------------

    //! The host created, replaced or destroyed the child window: place it and re-read limits.
    void child_changed() noexcept;
    void on_child_limits_changed() noexcept;
    [[nodiscard]] const RECT& content_rect() const noexcept { return content_; }

    // Playlists --------------------------------------------------------------------------------

    //! Rebuilds visible_, labels and the strip items from the model.
    void rebuild_strip() noexcept;
    //! visible_ from the model, in strip order: tabs pinned to the start, the unpinned ones, tabs
    //! pinned to the end, each group in playlist order. Sets pins_start_ / pins_end_.
    void collect_visible();
    //! Where `playlist` goes in visible_ within the group of `pin` (0 unpinned, 1 start, 2 end).
    [[nodiscard]] std::size_t group_insert_pos(std::size_t playlist, std::uint8_t pin) const noexcept;
    void insert_visible(std::size_t pos, std::size_t playlist, std::uint8_t pin);
    void erase_visible(std::size_t pos) noexcept;
    void update_label(std::size_t playlist) noexcept;
    //! Title formatting is on (labels_ holds one title per playlist; empty otherwise).
    [[nodiscard]] bool format_titles() const noexcept;
    //! The script may show a playlist's position: inserts, removals and moves relabel everything.
    [[nodiscard]] bool titles_follow_position() const noexcept;
    //! Inspects the title script (title_fields_) and asks the model for the item events it needs.
    void sync_title_needs() noexcept;
    //! Re-runs the title of `playlist`; updates its tab only if the text changed.
    bool relabel_if_changed(std::size_t playlist) noexcept;
    //! Drops titles of playlists that no longer exist.
    void prune_labels() noexcept;
    //! What tab `playlist` shows.
    [[nodiscard]] const std::wstring& label_of(std::size_t playlist) const noexcept;
    [[nodiscard]] StripItem make_item(std::size_t playlist) const;
    // Keyed incremental strip updates (one tab per event; layouts of the other tabs kept).
    void strip_playlist_created(std::size_t playlist) noexcept;
    void strip_playlist_removed(std::size_t playlist) noexcept;
    void strip_playlists_reordered() noexcept;
    void strip_playlist_flags(std::size_t playlist) noexcept;
    void strip_playlist_relabel(std::size_t playlist) noexcept;
    //! Refreshes strip tab `pos` (showing `playlist`) from make_item().
    void strip_relabel_at(std::size_t pos, std::size_t playlist) noexcept;
    //! After tabs came or went: strip size, visibility (two or more), limits.
    void strip_structure_changed() noexcept;
    [[nodiscard]] std::size_t strip_index_of_playlist(std::size_t playlist) const noexcept;
    [[nodiscard]] std::size_t playlist_of_strip(std::size_t strip_index) const noexcept;
    //! Makes `playlist` the active one (the playlist manager tells us back).
    void activate_playlist(std::size_t playlist, bool from_user) noexcept;
    void set_playlist_hidden(std::size_t playlist, bool hidden) noexcept;
    //! Locks the playlist with our lock, or unlocks it; beeps when another component's lock is on.
    void toggle_lock(std::size_t playlist) noexcept;
    //! Sorts the playlists at `positions` (playlist indices, ascending; empty = all) among
    //! themselves; the others keep their places. By length the sum runs on a CPU worker and the
    //! order is applied afterwards, if those playlists are still where they were.
    void sort_playlists(SortKey key, std::vector<std::size_t> positions) noexcept;
    //! Moves playlist `from` to position `to` (playlist indices).
    void move_playlist(std::size_t from, std::size_t to) noexcept;
    //! A copy right after `playlist`, activated. Returns its index, or SIZE_MAX.
    std::size_t duplicate_playlist(std::size_t playlist) noexcept;
    //! A mouse gesture's action (Settings::click_active_action and the others) on `playlist`.
    //! False when there is nothing to do (TabAction::none, no such playlist).
    bool run_tab_action(TabAction action, std::size_t playlist) noexcept;
    //! TabAction::show_now_playing: activates `playlist` and shows the playing track if it plays
    //! from there (focused and selected), else the focused track.
    void show_now_playing(std::size_t playlist) noexcept;
    //! TabAction::jump_first_last: focuses and selects the first track, or the last when the
    //! focus is already on the first.
    void jump_first_last(std::size_t playlist) noexcept;
    void save_playlist(std::size_t playlist) noexcept;
    void load_playlist() noexcept;
    //! Perf log only: creates 500 "EPT perf nnn" playlists, or removes them all.
    void perf_test_playlists(bool create) noexcept;
    //! Removal by a mouse gesture: asks first (Settings::confirm_remove, playlists with tracks). True to go ahead.
    bool confirm_remove(std::size_t playlist) noexcept;
    void rename_playlist(std::size_t playlist) noexcept;
    void remove_playlist(std::size_t playlist) noexcept;
    void new_playlist(std::size_t at) noexcept;
    [[nodiscard]] bool playlist_allows(std::size_t playlist, std::uint32_t lock_filter) const noexcept;
    //! A playlist that may take the active role when the active one is hidden.
    [[nodiscard]] std::size_t fallback_visible(std::size_t from) const noexcept;

    // Layout ----------------------------------------------------------------------------------

    [[nodiscard]] bool want_strip() const noexcept;
    void layout() noexcept;
    [[nodiscard]] Limits compute_limits() const noexcept;
    void limits_changed() noexcept;
    //! After settings_ changed at run time: clamp, push to the strip, lay out again.
    void apply_settings() noexcept;

    // Window ---------------------------------------------------------------------------------

    //! The messages every element handles the same way. False: not handled.
    bool core_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) noexcept;
    void on_create(HWND wnd) noexcept;
    void on_destroy() noexcept;
    void fill_background(HDC dc) const noexcept;
    //! fill_background, then (Settings::transparent_background) what our parent paints behind us.
    void paint_background(HDC dc) const noexcept;

    // Menus and dialogs ----------------------------------------------------------------------

    void show_tab_menu(std::size_t strip_index, POINT screen, bool full) noexcept;
    void append_style_menu(HMENU menu) const noexcept;
    void run_style_command(unsigned command) noexcept;
    //! `modeless`: the dialog stays open beside the element (only with a window; see
    //! open_configure_dialog) and this returns false at once. True: OK in the modal dialog.
    bool run_configure(HWND parent, bool modeless);

    // StripListener
    void on_strip_activate(std::size_t index) noexcept override;
    void on_strip_step(int direction) noexcept override;
    void on_strip_menu(std::size_t index, POINT screen) noexcept override;
    void on_strip_overflow(POINT screen) noexcept override;
    bool on_strip_key(UINT message, WPARAM key) noexcept override;
    void on_strip_metrics_changed() noexcept override;
    void on_strip_middle_click(std::size_t index) noexcept override;
    bool on_strip_double_click(std::size_t index) noexcept override;
    void on_strip_click_active(std::size_t index) noexcept override;
    void on_strip_reorder(std::size_t from, std::size_t to) noexcept override;
    void on_strip_reorder_block(std::span<const std::size_t> moved, std::size_t neighbour,
                                bool before) noexcept override;
    void on_strip_pointer() noexcept override;
    void on_strip_paint_failed(const char* detail) noexcept override;
    // HotZoneListener
    void on_hot_zone(bool inside, bool clicked) noexcept override;
    // ConfigureTarget
    void preview(const ConfigureState& state) noexcept override;
    void configure_closed(bool ok, const ConfigureState& state) noexcept override;
    // cover::Listener
    void on_cover_accent_changed() noexcept override;
    void update_cover_subscription() noexcept;
    // playlists::Listener
    void on_playlists(playlists::Change change, std::size_t index) noexcept override;
    // DropHandler: files or tracks dragged over the strip.
    DWORD on_drop_enter(IDataObject* data, POINT screen, DWORD allowed) noexcept override;
    DWORD on_drop_over(POINT screen, DWORD allowed) noexcept override;
    void on_drop_leave() noexcept override;
    DWORD on_drop(IDataObject* data, POINT screen, DWORD allowed) noexcept override;
    //! The effect at `screen`; moves the drop highlight and the hover-switch timer.
    DWORD drop_track(POINT screen, DWORD allowed) noexcept;
    void drop_end() noexcept;
    void drop_after_auto_hide() noexcept;
    void on_drop_switch_timer() noexcept;
    //! A drag over the hidden auto-hide strip's hot zone reveals it (after the reveal delay);
    //! the strip's own drop target takes over once it is shown. Nothing can be dropped on the
    //! hot zone itself.
    class HotZoneDrop final : public DropHandler {
    public:
        explicit HotZoneDrop(SwitcherCore& core) noexcept : core_(core) {}
        DWORD on_drop_enter(IDataObject*, POINT, DWORD) noexcept override;
        DWORD on_drop_over(POINT, DWORD) noexcept override;
        void on_drop_leave() noexcept override;
        DWORD on_drop(IDataObject*, POINT, DWORD) noexcept override;

    private:
        SwitcherCore& core_;
    };
    void sync_hot_zone_drop() noexcept;

    // Auto-hide --------------------------------------------------------------------------------
    enum class AhTimer : std::uint8_t { none, reveal, hide };
    [[nodiscard]] bool auto_hide() const noexcept { return settings_.visibility == StripVisibility::auto_hide; }
    //! Auto-hide drawn over the child: chosen, and the strip really is a layered child.
    [[nodiscard]] bool ah_overlay() const noexcept {
        return auto_hide() && settings_.reveal_mode == RevealMode::overlay && strip_.layered();
    }
    void ah_update_mode() noexcept;
    [[nodiscard]] bool ah_pointer_or_pinned() const noexcept;
    void ah_evaluate() noexcept;
    void ah_set_shown(bool shown) noexcept;
    void ah_set_timer(AhTimer kind, unsigned ms) noexcept;
    void ah_on_timer(UINT_PTR id) noexcept;
    void ah_note_switch() noexcept;
    void ah_raise() noexcept;
    [[nodiscard]] bool ah_covered() const noexcept;
    static void ah_sync_parent_watch() noexcept;
    static void CALLBACK ah_on_parent_change(HWINEVENTHOOK hook, DWORD event, HWND wnd, LONG object, LONG child,
                                             DWORD thread, DWORD time) noexcept;
    [[nodiscard]] int ah_px(unsigned dip) const noexcept;

    Settings settings_{};
    std::vector<RawField> unknown_settings_;

    StripWindow strip_;
    //! strip index -> playlist index
    std::vector<std::size_t> visible_;
    //! visible_ starts with pins_start_ tabs pinned to the start and ends with pins_end_ pinned
    //! to the end; the unpinned ones between are sorted (binary search).
    std::size_t pins_start_{0};
    std::size_t pins_end_{0};
    std::vector<StripItem> items_;
    //! Title formatting only: the title shown, by Entry::key (so reorders carry titles along).
    //! Empty with playlist names, which are read from the model (no copy).
    std::unordered_map<std::uint64_t, std::wstring> labels_;
    //! TitleField bits the title script uses (0 with playlist names).
    std::uint32_t title_fields_{0};
    //! The playing playlist's key and playback state when titles show them (0 = none).
    std::uint64_t playing_key_{0};
    bool playback_on_{false};
    bool playback_paused_{false};
    //! Titles with queue fields only: the queue length and queued tracks per playlist key
    //! (only playlists with some; a short list, as queues are short).
    std::size_t queue_total_{0};
    std::vector<std::pair<std::uint64_t, std::size_t>> queue_counts_;
    //! Reads the queue into queue_total_ / queue_counts_. Main thread.
    void read_queue() noexcept;
    [[nodiscard]] std::size_t queued_in(std::uint64_t key) const noexcept;
    //! Scratch for reorder_items(), kept for its capacity.
    std::vector<std::uint64_t> strip_keys_;
    //! Inside on_playlists(): it logs, rebuild_strip() does not.
    bool in_playlist_event_{false};
    titleformat_object::ptr title_script_;
    std::string title_source_;

    // Drag and drop onto the strip (only while a drag is over it).
    StripDrop strip_drop_;
    StripDrop hot_zone_drop_;
    HotZoneDrop hot_zone_drop_handler_{*this};
    //! An OLE drag is over the strip or the hot zone: auto-hide keeps the strip shown.
    bool drop_hovering_{false};
    bool drop_ok_{false};
    //! Strip index under the drag, or no_index (empty space or none).
    std::size_t drop_tab_{no_index};

    bool subscribed_{false};
    bool strip_shown_{false};
    RECT content_{};
    //! The rectangle last given to the child window.
    RECT child_applied_{};
    HWND child_seen_{nullptr};
    Limits child_limits_{};
    Limits limits_{};
    COLORREF background_{RGB(255, 255, 255)};
    //! Fill for transparent children (HostColours::layout).
    COLORREF child_background_{RGB(255, 255, 255)};
    bool in_create_{false};
    bool cover_subscribed_{false};
    //! The Settings::font the strip's font was last built with; refresh_font() when it differs.
    TabFont applied_font_{};

    // Auto-hide state.
    HotZone hot_zone_;
    bool ah_shown_{false};
    AhTimer ah_timer_{AhTimer::none};
    //! A menu (or a dialog from it) of this strip is open.
    bool menu_pin_{false};
    //! The modeless Configure dialog, and what Cancel puts back. It pins the strip too.
    HWND configure_wnd_{nullptr};
    ConfigureState configure_original_;
    // The tab menu's Items submenu (Settings::tracks_menu): the empty popup, the playlists whose
    // tracks it shows, and the context menu built for them when the popup first opens.
    HMENU items_menu_{nullptr};
    std::vector<std::size_t> items_playlists_;
    service_ptr_t<contextmenu_manager> items_manager_;
    //! WM_INITMENUPOPUP for items_menu_: builds the track context menu into it.
    void fill_items_menu() noexcept;
    //! The tracks of `playlists`, in order.
    [[nodiscard]] static metadb_handle_list tracks_of(const std::vector<std::size_t>& playlists);
    ULONGLONG linger_until_{0};
    bool ah_animating_{false};
    float ah_progress_{0.0f};
    float ah_from_{0.0f};
    ULONGLONG ah_anim_start_{0};
};

} // namespace ept
