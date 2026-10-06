#pragma once

// Per-instance settings. Plain values with explicit defaults; no SDK, no Win32, so the codec and
// the offline tests can use it. Stored field by field (codec.cpp), never as a raw struct, so
// adding a field never invalidates an older layout and an older build can read a newer one.

#include <cstdint>
#include <string>

namespace ept {

enum class StripPosition : std::uint8_t { top, bottom, left, right };
enum class SideText : std::uint8_t { horizontal, rotated };
enum class TabSizing : std::uint8_t { fit, equal, fill };
enum class TabAlign : std::uint8_t { start, centre, end };
//! Which end of the strip the overflow chevron sits at.
enum class ChevronPosition : std::uint8_t { end, start };
enum class StripVisibility : std::uint8_t { always, never, two_or_more, auto_hide };
//! tab: a fill that runs down to the edge facing the panel (rounded on the far side only), so the
//! active tab reads as joined to the playlist. tab_outline: the same shape as a faint fill with a
//! solid outline on its three free sides. New values go at the end: the codec stores the number.
enum class Indicator : std::uint8_t { underline, pill, none, tab, tab_outline };
enum class AccentSource : std::uint8_t { selection, custom, cover };
enum class StripBackground : std::uint8_t { theme, custom, accent_tint };
//! What a middle click on a tab does. remove_playlist goes through the playlist manager, so the
//! playlist can be brought back with File > Restore playlist.
enum class MiddleClick : std::uint8_t { nothing, hide_tab, remove_playlist, toggle_lock };
//! What a double click on a tab does.
enum class TabDoubleClick : std::uint8_t { nothing, rename };
//! The name of a playlist made by dropping files on empty strip space.
enum class DropName : std::uint8_t { folder, autoname };
//! Where a tab's title comes from.
enum class TitleMode : std::uint8_t { playlist_name, format };
enum class RevealMode : std::uint8_t { overlay, push };
enum class ShowHideAnimation : std::uint8_t { none, slide, fade };

struct Settings {
    StripPosition position{StripPosition::top};
    //! Rotated since 0.3.1: horizontal text makes a side strip as wide as its longest title.
    SideText side_text{SideText::rotated};
    TabSizing sizing{TabSizing::fit};
    TabAlign align{TabAlign::start};
    ChevronPosition chevron_position{ChevronPosition::end};
    //! When the tabs do not fit, shorten the longest titles (ellipsis) before overflowing to
    //! the chevron. Off = overflow straight away and keep every visible title whole.
    bool shrink_titles{false};

    // Metrics, all in DIPs.
    std::uint16_t pad_x{12};
    std::uint16_t pad_y{6};
    std::uint16_t spacing{2};
    //! Strip thickness; 0 = from the font.
    std::uint16_t thickness{0};

    StripVisibility visibility{StripVisibility::always};
    Indicator indicator{Indicator::underline};
    AccentSource accent_source{AccentSource::selection};
    //! 0xAARRGGBB, used when accent_source == custom.
    std::uint32_t accent_argb{0xFF3EA6FFu};
    //! Opacity of the active tab's accent fill (pill, chip) in percent; 0 = automatic.
    std::uint8_t accent_strength{0};
    StripBackground strip_background{StripBackground::theme};
    //! 0xAARRGGBB, used when strip_background == custom.
    std::uint32_t background_argb{0xFF202020u};
    //! How much accent goes into an accent-tinted strip, in percent.
    std::uint8_t tint_strength{12};
    std::uint16_t corner_radius{4};
    bool chip{false};
    //! Tab switches animate: the indicator (underline, pill or chip fill) slides.
    bool animations{true};
    //! Length of the auto-hide show/hide animation.
    std::uint16_t animation_ms{150};
    //! Length of the tab switch animation.
    std::uint16_t switch_ms{150};

    bool wheel_cycles{true};
    MiddleClick middle_click{MiddleClick::nothing};
    //! A middle click that removes a playlist with tracks asks first.
    bool confirm_remove{true};
    //! Dragging a tab reorders the playlists themselves.
    bool drag_reorder{true};
    //! Ctrl+Tab / Ctrl+Shift+Tab cycle the tabs while focus is inside the container.
    bool ctrl_tab{true};
    //! Longest title in DIPs before it is cut with an ellipsis (and shown whole as a tooltip);
    //! 0 = no limit.
    std::uint16_t max_tab_width{240};

    // Playlists.
    TitleMode title_mode{TitleMode::playlist_name};
    //! Used when title_mode == format; fields in model/playlist_title.h.
    std::string title_format{"%title%"};
    //! A double click on empty strip space creates a playlist (the playlist manager's own
    //! "New Playlist", "New Playlist (2)"... naming) and activates it.
    bool dblclick_new{true};
    TabDoubleClick dblclick_tab{TabDoubleClick::nothing};
    //! folder: the dropped folder's name (or the common parent of the dropped files), falling
    //! back to autoname when there is none (tracks from another playlist, several drives).
    DropName drop_name{DropName::folder};
    //! When playback starts, activate the playlist it plays from.
    bool follow_playing{false};
    //! Pinned tabs show a pin glyph before their title.
    bool pin_icon{true};
    //! Sorting playlists: largest / Z first.
    bool sort_descending{false};
    //! Sorting by name skips a leading "a", "an" or "the".
    bool sort_ignore_articles{true};

    // Auto-hide.
    std::uint16_t hot_zone{6};
    std::uint16_t reveal_delay_ms{0};
    std::uint16_t hide_delay_ms{400};
    std::uint16_t linger_ms{700};
    RevealMode reveal_mode{RevealMode::overlay};
    ShowHideAnimation show_hide_animation{ShowHideAnimation::slide};

    [[nodiscard]] bool operator==(const Settings&) const = default;
};

//! Pulls every numeric field into its supported range. Enums are range-checked by the codec.
void clamp(Settings& settings) noexcept;

} // namespace ept
