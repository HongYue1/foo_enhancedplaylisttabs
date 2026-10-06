#include "strip_layout.h"

#include <algorithm>

namespace ept {

void layout_strip(const StripLayoutInput& in, StripLayout& out) {
    const std::size_t n = in.extents.size();
    out.tabs.assign(n, Span{});
    out.first = 0;
    out.last = 0;
    out.overflow = false;
    out.chevron = Span{};
    out.pinned_start = (std::min)(in.pinned_start, n);
    out.pinned_end = (std::min)(in.pinned_end, n - out.pinned_start);
    if (n == 0 || in.length <= 0) return;

    const int spacing = (std::max)(0, in.spacing);
    int widest = 0;
    for (const int e : in.extents) widest = (std::max)(widest, (std::max)(0, e));

    // Natural lengths before any stretching.
    for (std::size_t i = 0; i < n; ++i) {
        out.tabs[i].length = in.sizing == TabSizing::equal ? widest : (std::max)(0, in.extents[i]);
    }

    long long total = static_cast<long long>(spacing) * static_cast<long long>(n - 1);
    for (const Span& s : out.tabs) total += s.length;

    if (total > in.length && in.shrink_floor > 0) {
        // Shorten the longest tabs to a common cap (the shorter ones keep their length), never
        // below the floor. Binary search for the largest cap that fits.
        const long long gaps = static_cast<long long>(spacing) * static_cast<long long>(n - 1);
        const auto total_at = [&](int cap) {
            long long sum = gaps;
            for (const Span& s : out.tabs) {
                const int floor = (std::min)(s.length, in.shrink_floor);
                sum += (std::max)(floor, (std::min)(s.length, cap));
            }
            return sum;
        };
        if (total_at(0) <= in.length) {
            int lo = 0;
            int hi = widest;
            while (lo < hi) {
                const int mid = lo + (hi - lo + 1) / 2;
                if (total_at(mid) <= in.length) {
                    lo = mid;
                } else {
                    hi = mid - 1;
                }
            }
            for (Span& s : out.tabs) {
                const int floor = (std::min)(s.length, in.shrink_floor);
                s.length = (std::max)(floor, (std::min)(s.length, lo));
            }
            total = total_at(lo);
        }
    }

    if (total <= in.length) {
        out.first = 0;
        out.last = n;
        int slack = in.length - static_cast<int>(total);
        if (in.sizing == TabSizing::fill && slack > 0) {
            // Share the slack; the remainder goes to the first tabs, one pixel each.
            const int each = slack / static_cast<int>(n);
            int extra = slack % static_cast<int>(n);
            for (Span& s : out.tabs) {
                s.length += each + (extra > 0 ? 1 : 0);
                if (extra > 0) --extra;
            }
            slack = 0;
        }
        int pos = 0;
        if (in.align == TabAlign::centre) pos = slack / 2;
        if (in.align == TabAlign::end) pos = slack;
        for (Span& s : out.tabs) {
            s.start = pos;
            pos += s.length + spacing;
        }
        return;
    }

    // Overflow: a chevron at one end, the pinned tabs next to it at their ends, and between them
    // a window of whole unpinned tabs that contains the active one.
    out.overflow = true;
    const int chevron = std::clamp(in.chevron, 0, in.length);
    const bool chevron_first = in.chevron_position == ChevronPosition::start;
    out.chevron = chevron_first ? Span{0, chevron} : Span{in.length - chevron, chevron};
    const int gap = chevron > 0 ? spacing : 0;
    const int avail = (std::max)(0, in.length - chevron - gap);
    // Where the tabs begin: after the chevron when it leads.
    const int origin = chevron_first ? (std::min)(in.length, chevron + gap) : 0;

    const std::size_t ps = out.pinned_start;
    const std::size_t pe = out.pinned_end;
    const std::size_t mid_begin = ps;
    const std::size_t mid_end = n - pe;
    // Room the pinned tabs leave for the window, each with its gap towards the window.
    long long pinned = 0;
    for (std::size_t i = 0; i < ps; ++i) pinned += out.tabs[i].length + spacing;
    for (std::size_t i = mid_end; i < n; ++i) pinned += out.tabs[i].length + spacing;
    const long long room = static_cast<long long>(avail) - pinned;

    const auto fits = [&](std::size_t first, std::size_t last) {
        long long used = 0;
        for (std::size_t i = first; i < last; ++i) {
            used += out.tabs[i].length + (i > first ? spacing : 0);
        }
        return used <= room;
    };

    std::size_t first = mid_begin;
    std::size_t last = mid_begin;
    const bool active_mid = in.active != no_index && in.active >= mid_begin && in.active < mid_end;
    while (last < mid_end && fits(first, last + 1)) ++last;
    if (active_mid && in.active >= last) {
        last = in.active + 1;
        first = in.active;
        while (first > mid_begin && fits(first - 1, last)) --first;
        // Fill any room left after the active tab.
        while (last < mid_end && fits(first, last + 1)) ++last;
    }
    // One tab wider than everything: show it clipped. With pins, only if it is the active one
    // (otherwise the pinned tabs are all there is room for).
    if (last == first && first < mid_end && (ps + pe == 0 || active_mid)) last = first + 1;

    int pos = 0;
    const auto place = [&](std::size_t i) {
        out.tabs[i].start = origin + pos;
        out.tabs[i].length = (std::min)(out.tabs[i].length, (std::max)(0, avail - pos));
        pos += out.tabs[i].length + spacing;
        if (out.tabs[i].length == 0) pos -= spacing;
    };
    const auto skip = [&](std::size_t i) { out.tabs[i] = Span{origin + (std::min)(pos, avail), 0}; };
    for (std::size_t i = 0; i < ps; ++i) place(i);
    for (std::size_t i = mid_begin; i < mid_end; ++i) {
        if (i < first || i >= last) {
            skip(i);
        } else {
            place(i);
        }
    }
    for (std::size_t i = mid_end; i < n; ++i) place(i);
    out.first = first;
    out.last = last;
}

std::size_t hit_test_strip(const StripLayout& layout, int pos) noexcept {
    // Starts never decrease (tabs not shown sit where they would be, with no length), so one
    // search covers the pinned tabs and the window alike.
    std::size_t lo = 0;
    std::size_t hi = layout.tabs.size();
    // Placed tabs are sorted and non-overlapping: find the last one starting at or before pos.
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (layout.tabs[mid].start <= pos) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo == 0) return no_index;
    const std::size_t i = lo - 1;
    return pos < layout.tabs[i].end() ? i : no_index;
}

} // namespace ept
