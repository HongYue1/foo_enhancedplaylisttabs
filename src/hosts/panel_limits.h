#pragma once

// The size limits the element reports to its host (Columns UI's uie::size_limit / WM_GETMINMAXINFO,
// the Default UI's get_min_max_info). Pure, so test\layout_test.cpp can check it.

#include <windows.h>

#include <algorithm>

namespace ept {

inline constexpr LONG limit_cap = MAXSHORT;

struct Limits {
    unsigned min_width{0};
    unsigned min_height{0};
    unsigned max_width{static_cast<unsigned>(limit_cap)};
    unsigned max_height{static_cast<unsigned>(limit_cap)};
    [[nodiscard]] bool operator==(const Limits&) const = default;
};

//! What the strip adds to the hosted panel's limits.
struct StripShare {
    //! Shown and docked: an auto-hidden strip never counts (showing it must not resize the
    //! layout around us), a hidden one takes no room.
    bool counts{false};
    //! Top or bottom strip: it adds to the height.
    bool along_height{true};
    unsigned thickness{0};
    //! A panel is hosted. Without one the strip is all there is: its thickness is the minimum and
    //! the maximum along it, as Columns UI's own Playlist tabs report with no child, so a host
    //! that hands out the room a panel may take (Panel Stack Splitter) gives no more.
    bool has_child{true};
};

[[nodiscard]] inline Limits panel_limits(const Limits& child, const StripShare& strip) noexcept {
    Limits out = strip.has_child ? child : Limits{};
    out.max_width = (std::max)(out.max_width, out.min_width);
    out.max_height = (std::max)(out.max_height, out.min_height);
    if (!strip.counts) return out;
    const auto cap = static_cast<unsigned>(limit_cap);
    unsigned& min_along = strip.along_height ? out.min_height : out.min_width;
    unsigned& max_along = strip.along_height ? out.max_height : out.max_width;
    min_along = (std::min)(min_along + strip.thickness, cap);
    if (!strip.has_child) {
        max_along = min_along;
    } else if (max_along < cap) {
        max_along = (std::min)(max_along + strip.thickness, cap);
    }
    return out;
}

//! The strip's extent across: its thickness, but never more than the room the host gave us.
[[nodiscard]] inline int strip_extent(int thickness, int room) noexcept {
    return (std::min)(thickness, (std::max)(0, room));
}

} // namespace ept
