#pragma once

// The tab strip: the only thing this component paints. Its own child window, a sibling of the
// hosted element, so invalidating it never touches it. Knows nothing about foobar2000: the host
// hands it labels, a theme and a font, and it answers with intents through StripListener. The
// Default UI and Columns UI containers share it unchanged.
//
// Painting: one persistent 32 bpp DIB section; a Direct2D DC render target is bound to exactly
// the invalidated rectangle, draws it, and only that rectangle is copied to the screen. Text
// layouts, metrics and tab rectangles are built outside WM_PAINT and only read inside it.

#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "../model/settings.h"
#include "../platform/com_ptr.h"
#include "../platform/perf.h"
#include "strip_layout.h"

namespace ept {

struct StripTheme {
    //! The strip's background, drawn as is.
    COLORREF background{RGB(255, 255, 255)};
    COLORREF text{RGB(0, 0, 0)};
    //! Already made legible against the background by the host.
    COLORREF accent{RGB(0, 120, 215)};
    //! The accent for a strong fill behind text (pill, chip at high strength), or CLR_INVALID
    //! to use `accent`. On a light strip a cover accent has to be dark to read as a line or as
    //! text, and a dark yellow is olive; a fill carries black text, so it can keep the cover's
    //! own light colour. The strip moves from `accent` to this as the fill gets stronger.
    COLORREF fill_accent{CLR_INVALID};
    bool dark{false};
    //! Opacity of the active tab's accent fill (pill, chip), 0 = the automatic look.
    float active_fill{0.0f};
    [[nodiscard]] bool operator==(const StripTheme&) const = default;
};

struct StripFont {
    //! GDI description (the Default UI's font): used when `family` is empty.
    LOGFONTW font{};
    //! The DPI lfHeight is expressed in.
    unsigned font_dpi{96};
    //! Optional DirectWrite description (Columns UI 3+, the offline tests); wins over `font` when set.
    std::wstring family;
    DWRITE_FONT_WEIGHT weight{DWRITE_FONT_WEIGHT_NORMAL};
    DWRITE_FONT_STYLE style{DWRITE_FONT_STYLE_NORMAL};
    DWRITE_FONT_STRETCH stretch{DWRITE_FONT_STRETCH_NORMAL};
    float size_dip{0.0f};
    //! An IDWriteFontFallback (Windows 8.1+), applied when the text format can take it.
    com_ptr<IUnknown> fallback;
};

enum class TextAntialias : std::uint8_t { automatic, greyscale, aliased };

//! Text rendering options (antialiasing, GDI-compatible metrics, colour glyphs).
struct StripTextOptions {
    TextAntialias antialias{TextAntialias::automatic};
    bool gdi_compatible{false};
    bool gdi_natural{false};
    bool colour_glyphs{true};
    com_ptr<IDWriteRenderingParams> params;
};

//! One tab as the host describes it.
struct StripItem {
    //! Identity across updates, or 0. set_items() matches items by key (by position when 0), so
    //! a tab that only moved keeps its text layout.
    std::uint64_t key{0};
    std::wstring label;
    //! One glyph (one or two UTF-16 units), or empty. A Private Use Area code point is drawn
    //! with `icon_font` or, when that is empty, the system icon font (Segoe Fluent Icons, Segoe
    //! MDL2 Assets, Segoe UI Symbol: the first one installed); anything else with the label font
    //! and its fallback, so emoji work.
    std::wstring icon;
    std::wstring icon_font;
    //! Shown on hover when the label is clipped or empty, or when it differs from the label.
    //! Empty = the label (no copy needed for the usual case).
    std::wstring tooltip;
    //! 0 = not pinned, 1 = pinned to the start, 2 = pinned to the end. The host hands the tabs in
    //! groups: start pins first, end pins last. Pinned tabs never go to the overflow chevron.
    std::uint8_t pin{0};
    [[nodiscard]] bool operator==(const StripItem&) const = default;
    //! Same text layouts (label and icon); the tooltip and key do not matter.
    [[nodiscard]] bool same_look(const StripItem& o) const noexcept {
        return label == o.label && icon == o.icon && icon_font == o.icon_font;
    }
};

class StripListener {
public:
    virtual void on_strip_activate(std::size_t index) noexcept = 0;
    //! Arrow keys and the mouse wheel: -1 previous, +1 next.
    virtual void on_strip_step(int direction) noexcept = 0;
    //! Right click or the menu key. index is no_index over empty strip space.
    virtual void on_strip_menu(std::size_t index, POINT screen) noexcept = 0;
    //! The overflow chevron was clicked.
    virtual void on_strip_overflow(POINT screen) noexcept = 0;
    //! Keys the strip does not use itself (Tab, shortcuts). Return true if handled.
    virtual bool on_strip_key(UINT message, WPARAM key) noexcept = 0;
    //! The strip's thickness changed (font, DPI, settings): the host must lay out again.
    virtual void on_strip_metrics_changed() noexcept = 0;
    //! Middle click on a tab.
    virtual void on_strip_middle_click(std::size_t index) noexcept = 0;
    //! Double click on a tab, or on empty strip space (no_index; never the chevron). Return true
    //! if it did something; false makes the second click an ordinary press (activate, drag).
    virtual bool on_strip_double_click(std::size_t index) noexcept {
        (void)index;
        return false;
    }
    //! A drag moved tab `from` to position `to` (strip indices, both before the move). The strip
    //! already shows the new order; the host makes it real and sends the tabs again.
    virtual void on_strip_reorder(std::size_t from, std::size_t to) noexcept = 0;
    //! A drag moved the selected tabs `moved` (strip indices before the drag, ascending, one
    //! group) as a block next to tab `neighbour` (before it, or after it if !before).
    virtual void on_strip_reorder_block(std::span<const std::size_t> moved, std::size_t neighbour,
                                        bool before) noexcept = 0;
    //! The pointer entered or left the strip, mouse capture or keyboard focus changed. Auto-hide
    //! re-evaluates on it; nothing else needs it.
    virtual void on_strip_pointer() noexcept {}
    //! Painting failed (first failure of a run); `detail` says which step, for the log.
    virtual void on_strip_paint_failed(const char* detail) noexcept { (void)detail; }

protected:
    ~StripListener() = default;
};

class StripWindow {
public:
    StripWindow() noexcept = default;
    StripWindow(const StripWindow&) = delete;
    StripWindow& operator=(const StripWindow&) = delete;
    ~StripWindow();

    //! Created hidden; the host shows it.
    bool create(HWND parent, StripListener& listener) noexcept;
    void destroy() noexcept;
    [[nodiscard]] HWND hwnd() const noexcept { return wnd_; }

    void set_settings(const Settings& settings) noexcept;
    //! `fade`: blend to it over a few frames (a new cover) when animations are on and the strip
    //! is visible; otherwise at once.
    void set_theme(const StripTheme& theme, bool fade = false) noexcept;
    void set_font(const StripFont& font) noexcept;
    void set_text_options(const StripTextOptions& options) noexcept;
    //! Replaces all tabs. Items are matched to the current ones by key (by position for key 0);
    //! only tabs whose label or icon changed, and new ones, build text layouts.
    void set_items(std::span<const StripItem> items, std::size_t active) noexcept;
    //! One tab more at `index` (<= count). Builds one layout.
    void insert_item(std::size_t index, StripItem item, std::size_t active) noexcept;
    //! One tab less. Builds nothing.
    void erase_item(std::size_t index, std::size_t active) noexcept;
    //! New text for one tab; builds its layout only if the label or icon changed.
    void update_item(std::size_t index, StripItem item) noexcept;
    //! Same tabs in a new order, given as their keys. Builds nothing. False (and nothing
    //! changed) if the keys are not a permutation of the current ones: use set_items().
    bool reorder_items(std::span<const std::uint64_t> keys, std::size_t active) noexcept;
    [[nodiscard]] std::size_t item_count() const noexcept { return items_.size(); }
    //! Multiple selection (Ctrl+click toggles a tab, Shift+click selects a range, a plain click
    //! clears it). Kept by key across set_items() and reorders; never includes the chevron.
    [[nodiscard]] std::size_t selection_count() const noexcept { return selected_count_; }
    [[nodiscard]] bool is_selected(std::size_t index) const noexcept {
        return index < items_.size() && items_[index].selected;
    }
    //! Selected tabs in strip order.
    void selection(std::vector<std::size_t>& out) const;
    void clear_selection() noexcept;
    //! The glyph for a pinned tab's icon in the system icon font (a pin), or an emoji pin where the
    //! icon font has none (Windows 7).
    [[nodiscard]] static const std::wstring& pin_glyph() noexcept;
    //! The tab at `index` as last given, or nullptr.
    [[nodiscard]] const StripItem* item(std::size_t index) const noexcept {
        return index < items_.size() ? &items_[index].spec : nullptr;
    }
    [[nodiscard]] std::size_t active() const noexcept { return active_; }
    //! Labels only (the offline render test).
    void set_labels(std::span<const std::wstring> labels, std::size_t active) noexcept;
    //! Cheap: invalidates the old and new active tab only (unless the visible range moves).
    void set_active(std::size_t active) noexcept;

    //! Thickness across the strip in pixels at the current DPI.
    [[nodiscard]] int thickness() const noexcept { return thickness_; }

    //! The tab under `client` (strip client pixels), or no_index (empty space, chevron, outside).
    [[nodiscard]] std::size_t tab_at(POINT client) const noexcept { return hit_test(client); }
    [[nodiscard]] bool chevron_at(POINT client) const noexcept { return chevron_hit(client); }
    //! An OLE drag is over tab `index` (no_index: over none): drawn as hover. The strip gets no
    //! mouse messages during OLE drag and drop, so the host's drop target drives this.
    void set_drop_hover(std::size_t index) noexcept;

    //! Where tab `index` is drawn (empty if it is not), in strip client pixels.
    [[nodiscard]] RECT tab_bounds(std::size_t index) const noexcept { return tab_rect(index); }

    //! Offline render test only: render at this DPI instead of the window's (0 = the window's).
    void set_dpi_override(unsigned dpi) noexcept;

    //! Paint statistics since the last call.
    void take_paint_stats(perf::PaintStats& out) noexcept;
    //! Tab text layouts built since the last call (perf log, model_test).
    [[nodiscard]] std::uint32_t take_layouts_built() noexcept {
        const std::uint32_t n = layouts_built_;
        layouts_built_ = 0;
        return n;
    }

    //! Auto-hide over the panel: a layered child (Windows 8+), composed without touching the
    //! panel under it. False if Windows refused (then the strip stays an ordinary child).
    bool set_layered(bool layered) noexcept;
    [[nodiscard]] bool layered() const noexcept { return layered_; }
    //! Constant opacity of a layered strip (fade animation).
    void set_alpha(BYTE alpha) noexcept;
    //! Call when hiding the strip: a window hidden under the pointer gets no WM_MOUSELEAVE, and
    //! its hover state would be stale (and leave tracking dead) the next time it is shown.
    void forget_pointer() noexcept;
    //! Arms WM_MOUSELEAVE now, for a strip that just appeared under a pointer that has not moved.
    //! If the pointer is elsewhere, Windows posts the leave at once (and the host hears of it).
    void track_pointer() noexcept;
    //! A drag is in progress.
    [[nodiscard]] bool dragging() const noexcept { return dragging_; }

    //! Where the last create() spent its time (perf log): window class + window, DPI and UI
    //! state, text format + items.
    struct CreateTimings {
        //! RegisterClassExW (first strip only).
        double class_ms{0.0};
        //! CreateWindowExW in total, then split: call to WM_NCCREATE (CBT hooks and the like run
        //! here), WM_NCCREATE to WM_CREATE, WM_CREATE to return (WinEvent hooks, WM_SIZE...).
        double window_ms{0.0};
        double to_nccreate_ms{0.0};
        double to_create_ms{0.0};
        double after_create_ms{0.0};
        double state_ms{0.0};
        double text_ms{0.0};
    };
    [[nodiscard]] const CreateTimings& create_timings() const noexcept { return create_timings_; }

    //! Renders `dirty` into the back buffer. WM_PAINT and the offline render test use it.
    bool render(const RECT& dirty) noexcept;
    //! The back buffer (32 bpp BGRA, top-down, `stride` bytes per row), valid after render().
    [[nodiscard]] const std::uint8_t* pixels(int& width, int& height, int& stride) const noexcept;

private:
    struct Item {
        StripItem spec;
        com_ptr<IDWriteTextLayout> layout;
        int text_width{0};
        int text_height{0};
        //! Text width actually drawn: text_width, or less when the tab is clipped (ellipsis).
        int draw_width{0};
        com_ptr<IDWriteTextLayout> icon_layout;
        int icon_width{0};
        int icon_height{0};
        //! Space between the icon and the label (0 without either).
        int icon_gap{0};
        //! In the multiple selection.
        bool selected{false};
        //! Built for this font/DPI generation; a newer generation rebuilds the layouts.
        unsigned generation{0};
        [[nodiscard]] int content_width() const noexcept { return icon_width + icon_gap + text_width; }
    };

    static LRESULT CALLBACK window_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;
    LRESULT on_message(UINT msg, WPARAM wp, LPARAM lp) noexcept;

    void on_paint() noexcept;
    //! Takes the client size if width_/height_ disagree with it. True if they changed.
    bool sync_size() noexcept;
    void on_size(int width, int height) noexcept;
    void on_mouse_move(POINT pt) noexcept;
    void on_mouse_leave() noexcept;
    //! `keys`: the MK_* flags of the mouse message.
    void on_button_down(POINT pt, WPARAM keys) noexcept;
    void on_double_click(POINT pt, WPARAM keys) noexcept;
    //! Ctrl / Shift + click on tab `index`. True if it changed the selection (no activation then).
    bool select_click(std::size_t index, WPARAM keys) noexcept;
    void set_selected(std::size_t index, bool selected) noexcept;
    //! Recounts selected_count_ after the items changed shape.
    void recount_selection() noexcept;
    void on_button_up(POINT pt) noexcept;
    void on_middle_up(POINT pt) noexcept;
    void drag_to(POINT pt) noexcept;
    void end_drag(bool commit) noexcept;
    //! Esc: ends a drag without moving anything and releases the press.
    void cancel_drag() noexcept;
    //! A drag starting on a selected tab: gathers the selected tabs of its group at it. False
    //! (nothing changed) for a single-tab drag.
    bool begin_block_drag() noexcept;
    void block_drag_to(int pos) noexcept;
    //! Puts the tabs back in drag_keys_ order (a cancelled block drag).
    void restore_drag_order() noexcept;
    //! Moves item `from` to `to`, keeping the active and hover marks on their tabs.
    void move_item(std::size_t from, std::size_t to) noexcept;
    void ensure_tooltip() noexcept;
    void update_tooltip() noexcept;
    [[nodiscard]] bool tooltip_wanted(std::size_t index) const noexcept;
    void on_context_menu(LPARAM lp) noexcept;
    bool on_key(WPARAM key) noexcept;
    void check_dpi() noexcept;

    void rebuild_text_format() noexcept;
    [[nodiscard]] bool make_layout(IDWriteTextFormat* format, const std::wstring& text, float max_width,
                                   float max_height, IDWriteTextLayout** out) const noexcept;
    [[nodiscard]] D2D1_TEXT_ANTIALIAS_MODE text_antialias() const noexcept;
    void rebuild_items() noexcept;
    void build_item(Item& item) noexcept;
    //! After the item list changed shape: cancels a drag and the switch animation.
    void begin_item_change() noexcept;
    //! Metrics, layout, tooltip and a repaint after the item list changed.
    void end_item_change(std::size_t active) noexcept;
    [[nodiscard]] IDWriteTextFormat* icon_format(const std::wstring& family) noexcept;
    void relayout() noexcept;
    void update_thickness() noexcept;
    bool ensure_buffer(int width, int height) noexcept;
    bool ensure_target() noexcept;
    void release_buffer() noexcept;

    [[nodiscard]] bool horizontal() const noexcept;
    //! A side strip with text turned 90 degrees.
    [[nodiscard]] bool rotated() const noexcept;
    [[nodiscard]] RECT tab_rect(std::size_t index) const noexcept;
    [[nodiscard]] RECT chevron_rect() const noexcept;
    [[nodiscard]] std::size_t hit_test(POINT pt) const noexcept;
    [[nodiscard]] bool chevron_hit(POINT pt) const noexcept;
    void invalidate_tab(std::size_t index) noexcept;
    [[nodiscard]] int px(int dips) const noexcept;
    //! content_width() with the title cut to Settings::max_tab_width.
    [[nodiscard]] int capped_width(const Item& item) const noexcept;

    void draw_tab(std::size_t index) noexcept;
    //! The indicator is a tab (Indicator::tab, tab_outline): fills run on to the panel edge.
    [[nodiscard]] bool tab_shape() const noexcept;
    //! Stroke of the outlined tab: 1.5 DIPs in whole pixels.
    [[nodiscard]] float outline_width() const noexcept {
        return static_cast<float>((std::max)(1, MulDiv(3, static_cast<int>(dpi_), 192)));
    }
    void draw_chevron() noexcept;
    //! Opacity of the active tab's accent fill (pill or chip).
    [[nodiscard]] float active_fill_alpha() const noexcept;
    //! The active tab is filled with the accent (pill, tab, outlined tab).
    [[nodiscard]] bool accent_filled() const noexcept;

    // Tab switch animation (Settings::animations): the indicator slides from the old tab to the
    // new one; text colours change at once. Off, nothing below runs.
    void start_switch(std::size_t from) noexcept;
    void stop_switch() noexcept;
    void on_switch_timer() noexcept;
    //! Everything the moving indicator crosses: both tabs and the ones between.
    [[nodiscard]] RECT switch_rect() const noexcept;
    void draw_switch_indicator() noexcept;

    // Colour fade to a new theme (a new cover). theme_ is what is drawn, target_theme_ what was
    // asked for; they differ only while fading_.
    void show_theme(const StripTheme& theme) noexcept;
    void stop_theme_fade() noexcept;
    void on_theme_timer() noexcept;

    HWND wnd_{nullptr};
    StripListener* listener_{nullptr};

    Settings settings_{};
    StripTheme theme_{};
    StripTheme target_theme_{};
    StripTheme fade_from_{};
    std::uint64_t fade_start_{0};
    bool fading_{false};
    StripFont font_{};
    //! No text format is built before the host's first set_font(): it would be thrown away.
    bool font_set_{false};
    bool layered_{false};
    BYTE alpha_{255};
    CreateTimings create_timings_{};
    //! QueryPerformanceCounter at WM_NCCREATE / WM_CREATE of the window being created.
    long long nccreate_qpc_{0};
    long long create_qpc_{0};
    StripTextOptions text_options_{};
    //! The strip's own background, derived from the theme.
    COLORREF surface_{RGB(255, 255, 255)};
    D2D1_DRAW_TEXT_OPTIONS draw_text_options_{D2D1_DRAW_TEXT_OPTIONS_CLIP};
    unsigned dpi_{96};
    unsigned dpi_override_{0};
    int width_{0};
    int height_{0};
    int thickness_{0};

    com_ptr<IDWriteTextFormat> text_format_;
    //! Icon fonts at the current size: the system icon font (empty name) and any named ones.
    std::vector<std::pair<std::wstring, com_ptr<IDWriteTextFormat>>> icon_formats_;
    unsigned generation_{1};
    std::vector<Item> items_;
    //! Scratch for set_items() / reorder_items(), kept for its capacity.
    std::vector<Item> next_items_;
    std::vector<std::pair<std::uint64_t, std::size_t>> key_index_;
    std::vector<int> extents_;
    std::uint32_t layouts_built_{0};
    StripLayout layout_;
    int line_height_{0};

    std::size_t active_{no_index};
    std::size_t hover_{no_index};
    std::size_t selected_count_{0};
    //! Where a Shift+click range starts: the key of the tab last clicked (0 = the active tab).
    std::uint64_t anchor_key_{0};
    std::size_t anchor_index_{no_index};
    bool tracking_{false};
    bool focused_{false};
    bool hide_focus_{true};
    bool chevron_hover_{false};
    //! A shortcut consumed WM_SYSKEYDOWN; swallow the WM_SYSCHAR that follows (no beep).
    bool ignore_syschar_{false};
    int wheel_accumulator_{0};

    // Pressing and dragging a tab.
    std::size_t press_index_{no_index};
    POINT press_pt_{};
    bool dragging_{false};
    std::size_t drag_origin_{no_index};
    std::size_t drag_index_{no_index};
    //! Where in the dragged tab it was grabbed (pixels along the strip from its start; from the
    //! block's start in a block drag).
    int drag_grab_{0};
    //! Block drag: the block is drag_block_ tabs from drag_first_ (0: a single-tab drag).
    std::size_t drag_block_{0};
    std::size_t drag_first_{0};
    //! Block drag: the order before it, and the moved tabs' indices in it.
    std::vector<std::uint64_t> drag_keys_;
    std::vector<std::size_t> drag_moved_;
    std::uint64_t drag_key_{0}; //!< the grabbed tab
    //! A plain press on a selected tab: the selection ends on release, unless it was dragged.
    bool clear_on_release_{false};

    bool switching_{false};
    std::size_t switch_from_{no_index};
    std::uint64_t switch_start_{0};
    //! Eased progress, 0 at the old tab, 1 at the new one.
    float switch_t_{1.0f};

    // One tooltip, one tool: its rectangle follows the hovered tab.
    HWND tooltip_{nullptr};
    std::size_t tip_index_{no_index};
    std::wstring tip_text_;

    // Back buffer and target.
    HDC mem_dc_{nullptr};
    HBITMAP dib_{nullptr};
    HGDIOBJ old_bitmap_{nullptr};
    void* bits_{nullptr};
    int buffer_width_{0};
    int buffer_height_{0};
    com_ptr<ID2D1DCRenderTarget> target_;
    com_ptr<ID2D1SolidColorBrush> brush_;
    bool cleartype_{false};
    //! Consecutive failed paints, and why the last one failed.
    unsigned paint_failures_{0};
    char paint_error_[128]{};
    //! Top-left of the rectangle being rendered; drawing code works in client pixels.
    float origin_x_{0.0f};
    float origin_y_{0.0f};

    perf::PaintStats stats_{};
};

} // namespace ept
