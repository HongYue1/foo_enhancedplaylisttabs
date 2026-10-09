// Forum report (Panel Stack Splitter, EPT with no child panel): EPT took more height than the
// splitter gave it. EPT answers WM_GETMINMAXINFO so Columns UI can ask for its size limits. Does
// Windows itself clamp a child's size to that answer when a host sizes it with SetWindowPos or
// DeferWindowPos (DefWindowProc's WM_WINDOWPOSCHANGING asks WM_GETMINMAXINFO for top-level
// windows)? It does not (checked 2026-10, Windows 11): the size a host sets stands, so a taller
// element comes from the host honouring the limits we report (see panel_limits.h).
#include <windows.h>
#include <cstdio>

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s  minmax: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

constexpr LONG min_height = 40;
bool g_own_poschanging = false;

LRESULT CALLBACK parent_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) { return DefWindowProcW(w, m, wp, lp); }

LRESULT CALLBACK child_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.y = min_height;
        mmi->ptMaxTrackSize.y = 10000;
        return 0;
    }
    case WM_WINDOWPOSCHANGING:
        if (g_own_poschanging) return 0;
        break;
    default: break;
    }
    return DefWindowProcW(w, m, wp, lp);
}

LONG height_after_resize(HWND parent, bool own_poschanging, bool deferred) {
    g_own_poschanging = own_poschanging;
    const HWND child = CreateWindowExW(0, L"minmax_child", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 200,
                                       100, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (child == nullptr) return -1;
    if (deferred) {
        HDWP dwp = BeginDeferWindowPos(1);
        dwp = DeferWindowPos(dwp, child, nullptr, 0, 0, 200, 20, SWP_NOZORDER | SWP_NOACTIVATE);
        EndDeferWindowPos(dwp);
    } else {
        SetWindowPos(child, nullptr, 0, 0, 200, 20, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    RECT r{};
    GetWindowRect(child, &r);
    MINMAXINFO asked{};
    SendMessageW(child, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&asked));
    if (own_poschanging && asked.ptMinTrackSize.y != min_height) return -2;
    DestroyWindow(child);
    return r.bottom - r.top;
}

} // namespace

int main() {
    WNDCLASSW wc{};
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpfnWndProc = parent_proc;
    wc.lpszClassName = L"minmax_parent";
    RegisterClassW(&wc);
    wc.lpfnWndProc = child_proc;
    wc.lpszClassName = L"minmax_child";
    RegisterClassW(&wc);
    const HWND parent = CreateWindowExW(0, L"minmax_parent", L"", WS_POPUP, 0, 0, 400, 300, nullptr, nullptr,
                                        wc.hInstance, nullptr);
    if (parent == nullptr) return 2;
    const LONG plain = height_after_resize(parent, false, false);
    const LONG deferred = height_after_resize(parent, false, true);
    std::printf("      host sets 20 px to a child whose minimum is %ld: %ld px (SetWindowPos), %ld px (DeferWindowPos)\n",
                min_height, plain, deferred);
    check(plain == 20, "Windows does not clamp a child's SetWindowPos to its WM_GETMINMAXINFO");
    check(deferred == 20, "... nor DeferWindowPos");
    check(height_after_resize(parent, true, false) == 20, "a child that answers WM_WINDOWPOSCHANGING itself: the same");
    DestroyWindow(parent);
    std::printf("failures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
