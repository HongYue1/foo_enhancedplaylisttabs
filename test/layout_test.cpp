// Offline tests for src/strip/strip_layout.cpp. Run build_tests.bat.

#include <cstdio>
#include <vector>

#include "../src/hosts/panel_limits.h"
#include "../src/strip/strip_layout.h"

using namespace ept;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

StripLayout run(int length, TabSizing sizing, TabAlign align, const std::vector<int>& extents,
                std::size_t active = no_index, int spacing = 2, int chevron = 24) {
    StripLayoutInput in;
    in.length = length;
    in.sizing = sizing;
    in.align = align;
    in.spacing = spacing;
    in.chevron = chevron;
    in.extents = extents;
    in.active = active;
    StripLayout out;
    layout_strip(in, out);
    return out;
}

} // namespace

int main() {
    const std::vector<int> three = {50, 80, 60};
    {
        const StripLayout l = run(400, TabSizing::fit, TabAlign::start, three);
        check(!l.overflow && l.first == 0 && l.last == 3, "fit: all visible");
        check(l.tabs[0] == Span{0, 50} && l.tabs[1] == Span{52, 80} && l.tabs[2] == Span{134, 60}, "fit: positions");
    }
    {
        const StripLayout l = run(400, TabSizing::fit, TabAlign::end, three);
        check(l.tabs[2].end() == 400, "end alignment touches the far edge");
        const StripLayout c = run(400, TabSizing::fit, TabAlign::centre, three);
        check(c.tabs[0].start == (400 - 194) / 2, "centre alignment");
    }
    {
        const StripLayout l = run(400, TabSizing::equal, TabAlign::start, three);
        check(l.tabs[0].length == 80 && l.tabs[1].length == 80 && l.tabs[2].length == 80, "equal: widest wins");
    }
    {
        const StripLayout l = run(401, TabSizing::fill, TabAlign::start, three);
        int total = 0;
        for (const Span& s : l.tabs) total += s.length;
        check(total + 4 == 401 && l.tabs[2].end() == 401, "fill: exactly fills, remainder distributed");
    }
    {
        // Overflow keeps the active tab visible and places the chevron at the end.
        std::vector<int> many(20, 60);
        const StripLayout l = run(300, TabSizing::fit, TabAlign::start, many, 15);
        check(l.overflow, "overflow detected");
        check(l.first <= 15 && 15 < l.last, "active stays visible");
        check(l.chevron == Span{276, 24}, "chevron at the end");
        bool inside = true;
        for (std::size_t i = l.first; i < l.last; ++i) inside = inside && l.tabs[i].end() <= 276 - 2;
        check(inside, "visible tabs clear the chevron");
        bool hidden = true;
        for (std::size_t i = 0; i < many.size(); ++i) {
            if (i < l.first || i >= l.last) hidden = hidden && l.tabs[i].length == 0;
        }
        check(hidden, "off-window tabs have no length");
        check(hit_test_strip(l, l.tabs[15].start + 1) == 15, "hit test finds the active tab");
        check(hit_test_strip(l, 290) == no_index, "hit test on the chevron is no tab");
    }
    {
        // The chevron can lead instead: tabs start after it and still keep the active one.
        std::vector<int> many(20, 60);
        StripLayoutInput in;
        in.length = 300;
        in.spacing = 2;
        in.chevron = 24;
        in.chevron_position = ChevronPosition::start;
        in.extents = many;
        in.active = 15;
        StripLayout l;
        layout_strip(in, l);
        check(l.overflow && l.first <= 15 && 15 < l.last, "chevron at start: active stays visible");
        check(l.chevron == Span{0, 24}, "chevron at the start");
        bool inside = true;
        for (std::size_t i = l.first; i < l.last; ++i) {
            inside = inside && l.tabs[i].start >= 26 && l.tabs[i].end() <= 300;
        }
        check(inside, "chevron at start: tabs clear it and the far edge");
        check(l.tabs[l.first].start == 26, "chevron at start: first tab right after it");
        check(hit_test_strip(l, 10) == no_index, "chevron at start: hit test on it is no tab");
        check(hit_test_strip(l, l.tabs[15].start + 1) == 15, "chevron at start: hit test finds the active tab");
    }
    {
        const StripLayout l = run(30, TabSizing::fit, TabAlign::start, {500});
        check(l.first == 0 && l.last == 1 && l.tabs[0].length > 0 && l.tabs[0].end() <= 30, "single huge tab clipped");
    }
    {
        const StripLayout l = run(300, TabSizing::fit, TabAlign::start, three);
        check(hit_test_strip(l, 51) == no_index, "hit test in the gap is no tab");
        check(hit_test_strip(l, 0) == 0 && hit_test_strip(l, 131) == 1 && hit_test_strip(l, 133) == no_index &&
                  hit_test_strip(l, 134) == 2 && hit_test_strip(l, 193) == 2,
              "hit test edges");
        check(hit_test_strip(l, 194) == no_index && hit_test_strip(l, -1) == no_index, "hit test outside");
    }
    {
        const StripLayout l = run(0, TabSizing::fit, TabAlign::start, three);
        check(l.first == 0 && l.last == 0 && l.tabs.size() == 3, "zero length: nothing placed");
        const StripLayout e = run(300, TabSizing::fit, TabAlign::start, {});
        check(e.tabs.empty() && hit_test_strip(e, 5) == no_index, "no tabs");
    }
    {
        // Shrink before overflow: the long tab gives up length, the short ones keep theirs.
        const std::vector<int> tabs = {360, 100, 120, 90};
        StripLayoutInput in;
        in.length = 500;
        in.spacing = 2;
        in.chevron = 24;
        in.extents = tabs;
        in.shrink_floor = 60;
        StripLayout l;
        layout_strip(in, l);
        check(!l.overflow && l.first == 0 && l.last == 4, "shrink: all tabs stay, no chevron");
        check(l.tabs[1].length == 100 && l.tabs[2].length == 120 && l.tabs[3].length == 90, "shrink: short tabs untouched");
        check(l.tabs[0].length == 500 - 6 - 310 && l.tabs[3].end() == 500, "shrink: long tab takes exactly the rest");

        // Equal sizing shrinks every tab alike.
        in.sizing = TabSizing::equal;
        layout_strip(in, l);
        check(!l.overflow && l.tabs[0].length == l.tabs[3].length && l.tabs[3].end() <= 500, "shrink: equal stays equal");

        // Below the floor it overflows as before.
        in.sizing = TabSizing::fit;
        in.length = 200;
        layout_strip(in, l);
        check(l.overflow, "shrink: overflow once the floor does not fit");

        // A floor of 0 keeps the old behaviour.
        in.length = 500;
        in.shrink_floor = 0;
        layout_strip(in, l);
        check(l.overflow, "shrink: off without a floor");
    }
    {
        // Pinned tabs stay on screen when the tabs overflow: the first two at the start, the last
        // one next to the chevron at the end; only the tabs between them scroll.
        std::vector<int> many(20, 60);
        StripLayoutInput in;
        in.length = 400;
        in.spacing = 2;
        in.chevron = 24;
        in.extents = many;
        in.active = 12;
        in.pinned_start = 2;
        in.pinned_end = 1;
        StripLayout l;
        layout_strip(in, l);
        check(l.overflow && l.pinned_start == 2 && l.pinned_end == 1, "pins: overflow with pins");
        check(l.tabs[0] == Span{0, 60} && l.tabs[1] == Span{62, 60}, "pins: start pins lead");
        check(l.first >= 2 && l.first <= 12 && 12 < l.last && l.last <= 19, "pins: active in the window");
        check(l.tabs[19].length == 60 && l.tabs[19].end() <= 400 - 24 - 2, "pins: end pin before the chevron");
        check(l.tabs[19].start > l.tabs[l.last - 1].end(), "pins: end pin after the window");
        bool sorted = true;
        for (std::size_t i = 1; i < many.size(); ++i) sorted = sorted && l.tabs[i].start >= l.tabs[i - 1].start;
        check(sorted, "pins: starts never decrease");
        check(hit_test_strip(l, 30) == 0 && hit_test_strip(l, l.tabs[19].start + 5) == 19 &&
                  hit_test_strip(l, l.tabs[12].start + 5) == 12,
              "pins: hit test finds pins and the window");
        check(l.shows(0) && l.shows(19) && l.shows(12) && !l.shows(l.last), "pins: shows()");
        std::size_t lo = 0, hi = 0;
        l.group_of(1, lo, hi);
        check(lo == 0 && hi == 2, "pins: start group");
        l.group_of(19, lo, hi);
        check(lo == 19 && hi == 20, "pins: end group");
        l.group_of(12, lo, hi);
        check(lo == l.first && hi == l.last, "pins: window group");

        // An active pinned tab does not drag the window anywhere.
        in.active = 0;
        layout_strip(in, l);
        check(l.first == 2 && l.tabs[19].length == 60, "pins: active pin, window from the start");

        // The chevron at the start: pins sit right after it.
        in.chevron_position = ChevronPosition::start;
        layout_strip(in, l);
        check(l.chevron == Span{0, 24} && l.tabs[0].start == 26 && l.tabs[19].end() <= 400, "pins: chevron first");

        // Pins only, no room for the window: nothing else is forced in.
        std::vector<int> wide = {150, 150, 150, 150};
        in.extents = wide;
        in.length = 320;
        in.chevron_position = ChevronPosition::end;
        in.pinned_start = 1;
        in.pinned_end = 1;
        in.active = 0;
        layout_strip(in, l);
        check(l.first == l.last && l.tabs[0].length == 150 && l.tabs[3].length == 142, "pins: pins first, clipped last");

        // Without overflow pins change nothing.
        in.extents = three;
        in.length = 400;
        layout_strip(in, l);
        check(!l.overflow && l.tabs[2] == Span{134, 60} && l.shows(1), "pins: no overflow, same layout");
        l.group_of(1, lo, hi);
        check(lo == 1 && hi == 2, "pins: unpinned group without overflow");
    }
    {
        // Reusing the output does not reallocate once grown.
        std::vector<int> many(20, 60);
        StripLayoutInput in;
        in.length = 300;
        in.extents = many;
        StripLayout out;
        layout_strip(in, out);
        const Span* before = out.tabs.data();
        in.length = 5000;
        layout_strip(in, out);
        check(out.tabs.data() == before, "output storage reused");
    }

    {
        // Limits reported to the host (panel_limits). Forum report: with no child panel, Panel
        // Stack Splitter gave the element more height than the strip.
        const auto cap = static_cast<unsigned>(limit_cap);
        StripShare strip;
        strip.counts = true;
        strip.thickness = 28;
        strip.has_child = false;
        Limits none = panel_limits(Limits{}, strip);
        check(none.min_height == 28 && none.max_height == 28, "no child: the strip's thickness is min and max height");
        check(none.min_width == 0 && none.max_width == cap, "no child: the width stays free");
        strip.along_height = false;
        none = panel_limits(Limits{}, strip);
        check(none.min_width == 28 && none.max_width == 28 && none.max_height == cap, "no child, side strip: fixed width");
        strip.along_height = true;
        strip.has_child = true;
        Limits child;
        child.min_height = 50;
        const Limits with = panel_limits(child, strip);
        check(with.min_height == 78 && with.max_height == cap, "a child: strip added to its minimum, maximum stays open");
        child.max_height = 200;
        check(panel_limits(child, strip).max_height == 228, "a child with a maximum: the strip is added to it");
        child.max_height = 10; // below its own minimum
        check(panel_limits(child, strip).max_height == 78, "a child's maximum below its minimum is raised first");
        strip.counts = false;
        strip.has_child = false;
        check(panel_limits(Limits{}, strip) == Limits{}, "auto-hidden or hidden strip, no child: no limits");
        // A host that gives less room than the strip: the strip is cut to it, never larger.
        check(strip_extent(28, 20) == 20 && strip_extent(28, 0) == 0 && strip_extent(28, -5) == 0 &&
                  strip_extent(28, 100) == 28,
              "the strip never extends past the room the host gave");
    }

    std::printf("%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
