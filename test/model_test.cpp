// Offline test of the keyed incremental strip updates: the real StripWindow, 500
// tabs, and the number of text layouts each kind of change builds. One playlist created among
// 500 must build one layout, not 500; removals, moves, hide/show and tooltip-only changes build
// none. Also checks the resulting order and the active tab. Built and run by test\build_tests.bat.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "../src/platform/graphics.h"
#include "../src/platform/perf.h"
#include "../src/strip/strip_window.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

// perf.cpp needs the SDK; the strip only needs these.
namespace ept::perf {
bool enabled() noexcept { return false; }
bool use_setredraw() noexcept { return false; }
std::uint64_t allocation_count() noexcept { return 0; }
std::uint64_t now() noexcept {
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    return static_cast<std::uint64_t>(c.QuadPart);
}
double elapsed_ms(std::uint64_t a, std::uint64_t b) noexcept {
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    return static_cast<double>(b - a) * 1000.0 / static_cast<double>(f.QuadPart);
}
} // namespace ept::perf

using namespace ept;

namespace {

class NullListener : public StripListener {
public:
    void on_strip_activate(std::size_t) noexcept override {}
    void on_strip_step(int) noexcept override {}
    void on_strip_menu(std::size_t, POINT) noexcept override {}
    void on_strip_overflow(POINT) noexcept override {}
    bool on_strip_key(UINT, WPARAM) noexcept override { return false; }
    void on_strip_metrics_changed() noexcept override {}
    void on_strip_middle_click(std::size_t) noexcept override {}
    void on_strip_reorder(std::size_t, std::size_t) noexcept override {}
};

int failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::printf("  FAILED: %s\n", what);
        ++failures;
    }
}

StripItem make(std::uint64_t key) {
    StripItem item;
    item.key = key;
    item.label = L"Playlist " + std::to_wstring(key);
    return item;
}

//! The host's view: the items it would send, in order.
std::vector<StripItem> model;

bool same_as_model(const StripWindow& strip) {
    if (strip.item_count() != model.size()) return false;
    for (std::size_t i = 0; i < model.size(); ++i) {
        const StripItem* item = strip.item(i);
        if (item == nullptr || item->key != model[i].key || item->label != model[i].label) return false;
    }
    return true;
}

void step(StripWindow& strip, const char* name, std::uint32_t want_layouts, std::uint64_t t0) {
    const double ms = perf::elapsed_ms(t0, perf::now());
    const std::uint32_t built = strip.take_layouts_built();
    std::printf("  %-40s %4u layouts  %8.3f ms\n", name, built, ms);
    if (built != want_layouts) {
        std::printf("  FAILED: %s built %u layouts, expected %u\n", name, built, want_layouts);
        ++failures;
    }
    expect(same_as_model(strip), name);
}

} // namespace

int main() {
    SetProcessDPIAware();
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"ept_model_test";
    RegisterClassW(&wc);
    const HWND parent = CreateWindowExW(0, wc.lpszClassName, L"", WS_POPUP, 0, 0, 1200, 200, nullptr, nullptr,
                                        wc.hInstance, nullptr);
    if (parent == nullptr) return 3;
    NullListener listener;
    {
        StripWindow strip;
        if (!strip.create(parent, listener)) return 4;
        strip.set_dpi_override(96);
        strip.set_settings(Settings{});
        StripFont font;
        font.family = L"Segoe UI";
        font.size_dip = 12.0f;
        strip.set_font(font);
        SetWindowPos(strip.hwnd(), nullptr, 0, 0, 1200, strip.thickness(), SWP_NOZORDER | SWP_NOACTIVATE);
        (void)strip.take_layouts_built();

        constexpr std::uint64_t count = 500;
        std::uint64_t next_key = 1;
        for (std::uint64_t i = 0; i < count; ++i) model.push_back(make(next_key++));

        std::uint64_t t = perf::now();
        strip.set_items(model, 10);
        step(strip, "first build, 500 tabs", 500, t);
        expect(strip.active() == 10, "active after first build");

        t = perf::now();
        strip.set_items(model, 10);
        step(strip, "same items again", 0, t);

        // One playlist created at 250 (before the active one? no: after it).
        StripItem created = make(next_key++);
        model.insert(model.begin() + 250, created);
        t = perf::now();
        strip.insert_item(250, created, 10);
        step(strip, "insert_item at 250 of 500", 1, t);

        // Created before the active tab: the active mark moves with it.
        StripItem first = make(next_key++);
        model.insert(model.begin(), first);
        t = perf::now();
        strip.insert_item(0, first, 11);
        step(strip, "insert_item at 0", 1, t);
        expect(strip.active() == 11, "active follows an insert before it");

        // The same through the keyed set_items (the fallback path).
        StripItem keyed = make(next_key++);
        model.insert(model.begin() + 400, keyed);
        t = perf::now();
        strip.set_items(model, 11);
        step(strip, "set_items with one new key", 1, t);

        model.erase(model.begin() + 250);
        t = perf::now();
        strip.erase_item(250, 11);
        step(strip, "erase_item at 250", 0, t);

        model.erase(model.begin() + 100);
        t = perf::now();
        strip.set_items(model, 11);
        step(strip, "set_items with one key gone", 0, t);

        // Move one playlist from 5 to 300.
        {
            const StripItem moved = model[5];
            model.erase(model.begin() + 5);
            model.insert(model.begin() + 300, moved);
            std::vector<std::uint64_t> keys;
            for (const StripItem& item : model) keys.push_back(item.key);
            t = perf::now();
            const bool ok = strip.reorder_items(keys, 10);
            step(strip, "reorder_items (one moved)", 0, t);
            expect(ok, "reorder_items accepted a permutation");
            expect(strip.active() == 10, "active after reorder");
        }

        // Reverse everything through set_items.
        {
            std::vector<StripItem> reversed(model.rbegin(), model.rend());
            model = reversed;
            t = perf::now();
            strip.set_items(model, 0);
            step(strip, "set_items reversed", 0, t);
        }

        // A key the strip does not have: refused, nothing changed.
        {
            std::vector<std::uint64_t> keys;
            for (const StripItem& item : model) keys.push_back(item.key);
            keys[3] = 999999;
            t = perf::now();
            const bool ok = strip.reorder_items(keys, 0);
            step(strip, "reorder_items with an unknown key", 0, t);
            expect(!ok, "reorder_items refused an unknown key");
        }

        // Rename one.
        model[42].label = L"Renamed playlist";
        t = perf::now();
        strip.update_item(42, model[42]);
        step(strip, "update_item (rename)", 1, t);

        // Tooltip only: no layout.
        model[43].tooltip = L"A tooltip";
        t = perf::now();
        strip.update_item(43, model[43]);
        step(strip, "update_item (tooltip only)", 0, t);

        // Rename through set_items: only that tab.
        model[7].label = L"Renamed through set_items";
        t = perf::now();
        strip.set_items(model, 0);
        step(strip, "set_items with one label changed", 1, t);

        // Hide and show one (erase + insert of the same key).
        {
            const StripItem hidden = model[60];
            model.erase(model.begin() + 60);
            t = perf::now();
            strip.erase_item(60, 0);
            step(strip, "hide one (erase_item)", 0, t);
            model.insert(model.begin() + 60, hidden);
            t = perf::now();
            strip.insert_item(60, hidden, 0);
            step(strip, "show it again (insert_item)", 1, t);
        }

        // Everything new (a model reset): all of them.
        for (StripItem& item : model) item = make(next_key++);
        const std::uint32_t all = static_cast<std::uint32_t>(model.size());
        t = perf::now();
        strip.set_items(model, 0);
        step(strip, "set_items, every key new (reset)", all, t);

        // Keyless items still go by position (the render test relies on it).
        std::vector<std::wstring> labels{L"One", L"Two", L"Three"};
        model.clear();
        for (const std::wstring& l : labels) {
            StripItem item;
            item.label = l;
            model.push_back(item);
        }
        t = perf::now();
        strip.set_labels(labels, 0);
        step(strip, "set_labels, 3 keyless", 3, t);
        t = perf::now();
        strip.set_labels(labels, 1);
        step(strip, "set_labels again", 0, t);

        strip.destroy();
    }
    DestroyWindow(parent);
    gfx::shutdown();
    CoUninitialize();
    std::printf("failures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
