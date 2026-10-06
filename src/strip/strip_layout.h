#pragma once

// Where the tabs go along the strip. One dimension only ("along" = x for a top/bottom strip,
// y for a side strip), in whole device pixels, so edges are always crisp. Pure: no Win32, no
// allocation once `out` has grown to the tab count. test/layout_test.cpp covers it offline.

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

#include "../model/settings.h"

namespace ept {

inline constexpr std::size_t no_index = static_cast<std::size_t>(-1);

struct Span {
    int start{0};
    int length{0};
    [[nodiscard]] int end() const noexcept { return start + length; }
    [[nodiscard]] bool operator==(const Span&) const = default;
};

struct StripLayoutInput {
    //! Available length along the strip.
    int length{0};
    TabSizing sizing{TabSizing::fit};
    TabAlign align{TabAlign::start};
    int spacing{0};
    //! Length of the overflow chevron, used only when the tabs do not fit.
    int chevron{0};
    //! Which end of the strip the chevron sits at.
    ChevronPosition chevron_position{ChevronPosition::end};
    //! Natural length of each tab: text plus padding.
    std::span<const int> extents;
    //! Kept on screen when the tabs overflow. no_index = none.
    std::size_t active{no_index};
    //! When the tabs do not fit, the longest are shortened first (ellipsis), but never below this
    //! (or their natural length, if shorter). Only when even that does not fit do they overflow
    //! to the chevron. 0 = never shorten.
    int shrink_floor{0};
    //! The first `pinned_start` and the last `pinned_end` tabs are pinned: when the tabs overflow
    //! they stay on screen at their end of the strip, between the chevron and the scrolling window,
    //! and only the tabs between them go to the chevron. Clamped to the tab count.
    std::size_t pinned_start{0};
    std::size_t pinned_end{0};
};

struct StripLayout {
    //! One per input tab. Tabs that are not shown have length 0 and are not drawn; their start is
    //! where they would be, so starts never decrease along the strip (hit_test_strip relies on it).
    std::vector<Span> tabs;
    //! The window of unpinned tabs shown, within [pinned_start, tabs.size() - pinned_end).
    std::size_t first{0};
    std::size_t last{0};
    //! The pinned tabs (clamped input): [0, pinned_start) and [tabs.size() - pinned_end, size).
    std::size_t pinned_start{0};
    std::size_t pinned_end{0};
    bool overflow{false};
    Span chevron;
    //! Tab `i` is in the shown set (pinned or in the window); it may still be clipped to nothing.
    [[nodiscard]] bool shows(std::size_t i) const noexcept {
        return i < tabs.size() && (i < pinned_start || i >= tabs.size() - pinned_end || (i >= first && i < last));
    }
    //! The tabs `i` can trade places with by dragging: its pin group, or the shown window.
    void group_of(std::size_t i, std::size_t& lo, std::size_t& hi) const noexcept {
        const std::size_t n = tabs.size();
        if (i < pinned_start) {
            lo = 0;
            hi = pinned_start;
        } else if (i >= n - pinned_end) {
            lo = n - pinned_end;
            hi = n;
        } else {
            lo = (std::max)(first, pinned_start);
            hi = (std::min)(last, n - pinned_end);
        }
    }
};

//! Reuses `out`'s storage.
void layout_strip(const StripLayoutInput& in, StripLayout& out);

//! Index of the tab at `pos` along the strip, or no_index. Binary search on the placed tabs.
[[nodiscard]] std::size_t hit_test_strip(const StripLayout& layout, int pos) noexcept;

} // namespace ept
