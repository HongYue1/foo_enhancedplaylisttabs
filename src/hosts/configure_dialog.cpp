// The Configure dialog and the Rename dialog. Layout: foo_enhancedplaylisttabs.rc (style profile at the top);
// conventions: foobar2000-component-dev/references/preferences-pages.md (one child dialog per
// page with its own dark-mode hooks, guarded WM_NOTIFY, padded edits, swatch + hex colours).

#include <helpers/foobar2000+atl.h>

#include <helpers/DarkMode.h>
#include <helpers/atl-misc.h>

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

#include "fbc/fonts.h"

#include "../../resource.h"
#include "configure_dialog.h"

#include "../playlists/playlist_title.h"

namespace ept {

namespace {

constexpr int page_count = 9;

//! A control by id on the dialog itself or on one of its pages (ids are unique across pages).
[[nodiscard]] HWND find_control(HWND dialog, int control) {
    if (HWND direct = ::GetDlgItem(dialog, control)) return direct;
    for (HWND w = ::GetWindow(dialog, GW_CHILD); w != nullptr; w = ::GetWindow(w, GW_HWNDNEXT)) {
        wchar_t name[16]{};
        ::GetClassNameW(w, name, 16);
        if (wcscmp(name, L"#32770") != 0) continue;
        if (HWND found = ::GetDlgItem(w, control)) return found;
    }
    return nullptr;
}

[[nodiscard]] std::wstring window_text(HWND wnd) {
    if (wnd == nullptr) return {};
    const int length = ::GetWindowTextLengthW(wnd);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    const int got = ::GetWindowTextW(wnd, text.data(), length + 1);
    text.resize(static_cast<std::size_t>((std::max)(0, got)));
    return text;
}

[[nodiscard]] std::string to_utf8(const std::wstring& text) {
    return std::string(pfc::stringcvt::string_utf8_from_wide(text.c_str()).get_ptr());
}

[[nodiscard]] std::wstring to_wide(const std::string& text) {
    return std::wstring(pfc::stringcvt::string_wide_from_utf8(text.c_str(), text.size()).get_ptr());
}

//! Six hex digits. Junk reads as the fallback rather than as black, which would look like a bug.
[[nodiscard]] std::uint32_t parse_rgb(const std::wstring& text, std::uint32_t fallback) {
    std::wstring t = text;
    if (!t.empty() && t.front() == L'#') t.erase(0, 1);
    if (t.empty() || t.size() > 6) return fallback;
    std::uint32_t value = 0;
    for (const wchar_t c : t) {
        std::uint32_t digit = 0;
        if (c >= L'0' && c <= L'9') {
            digit = static_cast<std::uint32_t>(c - L'0');
        } else if (c >= L'a' && c <= L'f') {
            digit = static_cast<std::uint32_t>(c - L'a' + 10);
        } else if (c >= L'A' && c <= L'F') {
            digit = static_cast<std::uint32_t>(c - L'A' + 10);
        } else {
            return fallback;
        }
        value = (value << 4) | digit;
    }
    return 0xFF000000u | value;
}

[[nodiscard]] std::wstring format_rgb(std::uint32_t argb) {
    wchar_t buffer[8]{};
    std::swprintf(buffer, 8, L"%06X", argb & 0xFFFFFFu);
    return buffer;
}

[[nodiscard]] COLORREF colorref(std::uint32_t argb) {
    return RGB((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF);
}

//! Text fields get some room before the first character (fb2k-common).
using fbc::fonts::pad_text_fields;

[[nodiscard]] fbc::fonts::FontChoice font_choice(const TabFont& font) {
    return {font.family, font.tenths_pt, font.weight, font.italic};
}

//! titleformat_help.html ships with foobar2000, next to the executable.
void open_titleformat_help(HWND owner) {
    wchar_t module[MAX_PATH]{};
    const DWORD length = ::GetModuleFileNameW(nullptr, module, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return;
    std::wstring path(module, length);
    const std::size_t slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos) return;
    path.erase(slash + 1);
    path += L"doc\\titleformat_help.html";
    ::ShellExecuteW(owner, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

//! Ready-made tab titles for the Examples list (Titles page). Picking one puts it in the field.
constexpr const wchar_t* title_examples[] = {
    // Parentheses, brackets and commas are syntax outside quotes: write literal ones as '(' ')'.
    L"%title% '('%size%')'",
    L"%title% '('%length%')'",
    L"%index%. %title%",
    L"$if(%is_playing%,\u00BB )%title%",
    L"$if(%is_locked%,'['%lock_name%']' )%title%",
    L"$if(%is_active%,%title% \u00B7 %size%,%title%)",
    L"$left(%title%,16)",
    L"$upper(%title%)",
};

//! Fills a drop-down in enum order, so the selection index *is* the stored value.
void fill_combo(HWND combo, std::initializer_list<const wchar_t*> items) {
    if (combo == nullptr) return;
    ::SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const wchar_t* item : items) ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
}

//! An owner-drawn colour swatch on its page's own background (right in light and dark mode).
void draw_swatch(const DRAWITEMSTRUCT& item, COLORREF colour) {
    const HDC dc = item.hDC;
    const HWND page = ::GetParent(item.hwndItem);
    auto brush = reinterpret_cast<HBRUSH>(
        ::SendMessageW(page, WM_CTLCOLORDLG, reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(page)));
    ::FillRect(dc, &item.rcItem, brush != nullptr ? brush : ::GetSysColorBrush(COLOR_BTNFACE));
    const bool dark = DarkMode::IsDialogDark(page);
    RECT swatch = item.rcItem;
    ::InflateRect(&swatch, -1, -1);
    COLORREF fill = colour;
    if ((item.itemState & ODS_DISABLED) != 0) {
        const COLORREF back = dark ? RGB(32, 32, 32) : ::GetSysColor(COLOR_BTNFACE);
        fill = RGB((GetRValue(fill) + 2 * GetRValue(back)) / 3, (GetGValue(fill) + 2 * GetGValue(back)) / 3,
                   (GetBValue(fill) + 2 * GetBValue(back)) / 3);
    }
    const HBRUSH fill_brush = ::CreateSolidBrush(fill);
    ::FillRect(dc, &swatch, fill_brush);
    ::DeleteObject(fill_brush);
    const HBRUSH frame = ::CreateSolidBrush(dark ? RGB(130, 130, 130) : ::GetSysColor(COLOR_BTNSHADOW));
    ::FrameRect(dc, &swatch, frame);
    ::DeleteObject(frame);
    if ((item.itemState & ODS_FOCUS) != 0 && (item.itemState & ODS_NOFOCUSRECT) == 0) {
        ::DrawFocusRect(dc, &item.rcItem);
    }
}

//! The system colour picker; false if cancelled.
[[nodiscard]] bool pick_colour(HWND owner, std::uint32_t& argb) {
    static COLORREF custom[16]{};
    CHOOSECOLORW dialog{};
    dialog.lStructSize = sizeof dialog;
    dialog.hwndOwner = owner;
    dialog.rgbResult = colorref(argb);
    dialog.lpCustColors = custom;
    dialog.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!::ChooseColorW(&dialog)) return false;
    const COLORREF c = dialog.rgbResult;
    argb = 0xFF000000u | (static_cast<std::uint32_t>(GetRValue(c)) << 16) |
           (static_cast<std::uint32_t>(GetGValue(c)) << 8) | GetBValue(c);
    return true;
}

//! A colour swatch button, its hex field and the setting they edit.
struct Swatch {
    int button;
    int hex;
    std::uint32_t Settings::*field;
};
constexpr Swatch swatches[] = {
    {IDC_ACCENT_SWATCH, IDC_ACCENT_HEX, &Settings::accent_argb},
    {IDC_BACKGROUND_SWATCH, IDC_BACKGROUND_HEX, &Settings::background_argb},
    {IDC_HOVER_SWATCH, IDC_HOVER_HEX, &Settings::hover_argb},
};
//! The Hover page edits one of two sets: the other tabs' (Settings::hover_*) or the active tab's.
struct HoverFields {
    HoverStyle Settings::*style;
    HoverColour Settings::*colour;
    std::uint32_t Settings::*argb;
    std::uint8_t Settings::*fill_strength;
    std::uint8_t Settings::*line_width;
    std::uint8_t Settings::*line_opacity;
};
constexpr HoverFields hover_others{&Settings::hover_style,         &Settings::hover_colour,
                                   &Settings::hover_argb,          &Settings::hover_fill_strength,
                                   &Settings::hover_line_width,    &Settings::hover_line_opacity};
constexpr HoverFields hover_active{&Settings::active_hover_style,         &Settings::active_hover_colour,
                                   &Settings::active_hover_argb,          &Settings::active_hover_fill_strength,
                                   &Settings::active_hover_line_width,    &Settings::active_hover_line_opacity};
//! The style list: the HoverStyle values in order; the active tab's starts with its plain wash.
[[nodiscard]] int hover_style_index(HoverStyle style, bool active) noexcept {
    if (!active) return style == HoverStyle::plain ? 0 : static_cast<int>(style);
    return style == HoverStyle::plain ? 0 : static_cast<int>(style) + 1;
}
[[nodiscard]] HoverStyle hover_style_at(LRESULT index, bool active) noexcept {
    if (active) {
        if (index == 0) return HoverStyle::plain;
        --index;
    }
    return index >= 0 && index <= static_cast<LRESULT>(HoverStyle::none) ? static_cast<HoverStyle>(index) : HoverStyle::fill;
}
//! `button` must be one of the swatches; anything else gets the first.
[[nodiscard]] const Swatch& swatch_for(int button) {
    for (const Swatch& swatch : swatches) {
        if (swatch.button == button) return swatch;
    }
    return swatches[0];
}

// ---------------------------------------------------------------------------------------------

class ConfigureDialog : public CDialogImpl<ConfigureDialog> {
public:
    enum { IDD = IDD_CONFIGURE };

    ConfigureDialog(ConfigureState& state, ConfigureTarget& target, bool live)
        : state_(state), target_(target), live_(live) {}

    BEGIN_MSG_MAP_EX(ConfigureDialog)
        MSG_WM_INITDIALOG(on_init_dialog)
        MSG_WM_COMMAND(on_command)
        MSG_WM_DRAWITEM(on_draw_item)
        MESSAGE_HANDLER_EX(WM_HSCROLL, on_hscroll)
        MESSAGE_HANDLER_EX(WM_NOTIFY, on_notify)
    END_MSG_MAP()

private:
    BOOL on_init_dialog(CWindow, LPARAM);
    void on_command(UINT code, int id, CWindow control);
    void on_draw_item(UINT, LPDRAWITEMSTRUCT item);
    LRESULT on_hscroll(UINT, WPARAM, LPARAM);
    //! TCN_SELCHANGE. Raw WM_NOTIFY: dialogs have been sent WM_NOTIFY with an lParam that is no
    //! pointer at all, which crashes a handler that reads the header.
    LRESULT on_notify(UINT, WPARAM, LPARAM lparam);

    void create_pages();
    //! Pages keep nothing themselves: commands, owner-draw and scroll messages go to the dialog.
    static INT_PTR CALLBACK page_proc(HWND, UINT, WPARAM, LPARAM);
    void show_page(int page);

    [[nodiscard]] HWND control(int id) const { return find_control(m_hWnd, id); }
    [[nodiscard]] bool checked(int id) const {
        return ::SendMessageW(control(id), BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    void check(int id, bool on) const {
        ::SendMessageW(control(id), BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    //! Enables a control; on a change, repaints the page behind it (see repaint_behind).
    void enable(int id, bool on) const {
        const HWND w = control(id);
        if (w == nullptr || (::IsWindowEnabled(w) != FALSE) == on) return;
        ::EnableWindow(w, on ? TRUE : FALSE);
        repaint_behind(w);
    }
    //! Sets a label's text only when it changes, then repaints the page behind it.
    void set_label(int id, const wchar_t* text) const {
        const HWND w = control(id);
        if (w == nullptr) return;
        wchar_t current[256]{};
        ::GetWindowTextW(w, current, 256);
        if (std::wcscmp(current, text) == 0) return;
        ::SetWindowTextW(w, text);
        repaint_behind(w);
    }
    //! Dark mode draws static text on a transparent background, so a static repainted on its own
    //! (new text, enabled or disabled) draws over its old glyphs, which pile up into a bold,
    //! fringed look. Erasing the page behind it first draws it once, cleanly.
    static void repaint_behind(HWND w) {
        const HWND page = ::GetParent(w);
        RECT r{};
        ::GetWindowRect(w, &r);
        ::MapWindowPoints(HWND_DESKTOP, page, reinterpret_cast<POINT*>(&r), 2);
        ::RedrawWindow(page, &r, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }
    [[nodiscard]] LRESULT selection(int id) const { return ::SendMessageW(control(id), CB_GETCURSEL, 0, 0); }
    void select(int id, int index) const { ::SendMessageW(control(id), CB_SETCURSEL, static_cast<WPARAM>(index), 0); }
    [[nodiscard]] std::uint16_t number(int id) const;
    void set_number(int id, unsigned value) const { ::SetDlgItemInt(::GetParent(control(id)), id, value, FALSE); }
    [[nodiscard]] int slider(int id) const {
        return static_cast<int>(::SendMessageW(control(id), TBM_GETPOS, 0, 0));
    }
    void set_slider(int id, int lo, int hi, int value) const;

    void settings_to_controls();
    void settings_from_controls();
    void update_values();
    void update_enabled();
    //! The Fonts page's rows, from state_.settings.font.
    void show_fonts();
    //! Select, Default and Clear on the Fonts page. False for any other control.
    bool on_font_command(int id);
    //! "Active playlist: <its tab title>" under the title field.
    void update_preview();


    void changed();

    ConfigureState& state_;
    ConfigureTarget& target_;
    const bool live_;
    bool loading_{false};
    //! The Hover page shows the active tab's settings (else the other tabs').
    bool hover_active_{false};
    [[nodiscard]] const HoverFields& hover_fields() const noexcept { return hover_active_ ? hover_active : hover_others; }
    [[nodiscard]] std::uint32_t Settings::*swatch_field(const Swatch& swatch) const noexcept {
        return swatch.button == IDC_HOVER_SWATCH ? hover_fields().argb : swatch.field;
    }
    void fill_hover_styles();
    void hover_to_controls();
    HWND pages_[page_count]{};
    // Must be a member: it hooks the dialog and its controls for the lifetime of both.
    fb2k::CDarkModeHooks dark_;
};

BOOL ConfigureDialog::on_init_dialog(CWindow, LPARAM) {
    dark_.AddDialogWithControls(*this);
    create_pages();
    {
        const HWND tabs = ::GetDlgItem(m_hWnd, IDC_TABS);
        const wchar_t* const names[page_count] = {L"Strip", L"Look", L"Hover", L"Colours", L"Fonts", L"Titles", L"Behaviour", L"Mouse", L"Auto-hide"};
        for (int i = 0; i < page_count; ++i) {
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            item.pszText = const_cast<wchar_t*>(names[i]);
            ::SendMessageW(tabs, TCM_INSERTITEMW, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(&item));
        }
        // Only the strip is wanted, not an empty page frame under it.
        RECT item{};
        RECT client{};
        ::SendMessageW(tabs, TCM_GETITEMRECT, 0, reinterpret_cast<LPARAM>(&item));
        ::GetClientRect(tabs, &client);
        ::SetWindowPos(tabs, nullptr, 0, 0, client.right, item.bottom + 2, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        show_page(0);
    }
    fill_combo(control(IDC_POSITION), {L"Top", L"Bottom", L"Left", L"Right"});
    // Order matters: visibility_order below.
    fill_combo(control(IDC_VISIBILITY), {L"Always", L"Only with two or more playlists", L"Auto-hide", L"Never"});
    // Order matters: RevealMode and ShowHideAnimation, one for one.
    fill_combo(control(IDC_AH_MODE), {L"Over the panel (fastest)", L"Push the panel aside"});
    fill_combo(control(IDC_AH_ANIM), {L"None", L"Slide", L"Fade"});
    fill_combo(control(IDC_SIZING), {L"Fit the title", L"All equal", L"Fill the strip"});
    fill_combo(control(IDC_ALIGN), {L"Start", L"Centre", L"End"});
    // Order matters: ChevronPosition.
    fill_combo(control(IDC_CHEVRON), {L"At the end of the strip", L"At the start of the strip"});
    // Order matters: Indicator, one for one.
    fill_combo(control(IDC_INDICATOR), {L"Underline", L"Pill", L"Text only", L"Tab", L"Outlined tab"});
    fill_combo(control(IDC_HOVER_TARGET), {L"Other tabs", L"The active tab"});
    ::SendMessageW(control(IDC_HOVER_TARGET), CB_SETCURSEL, hover_active_ ? 1 : 0, 0);
    fill_hover_styles();
    // Order matters: HoverColour and HoverText, one for one.
    fill_combo(control(IDC_HOVER_COLOUR), {L"Text colour", L"Accent colour", L"Custom colour"});
    fill_combo(control(IDC_HOVER_TEXT), {L"Brightens", L"Stays as it is", L"Takes the hover colour"});
    // Order matters: AccentSource and StripBackground, one for one.
    const std::wstring theme_accent = state_.ui_name + L" selection colour";
    const std::wstring theme_background = state_.ui_name + L" background";
    const std::wstring theme_highlight = state_.ui_name + L" " + state_.highlight_name;
    fill_combo(control(IDC_ACCENT_SOURCE),
               {theme_accent.c_str(), L"Custom colour", L"From the playing cover", theme_highlight.c_str()});
    fill_combo(control(IDC_BACKGROUND), {theme_background.c_str(), L"Custom colour", L"Tinted with the accent"});
    // Order matters: TabAction, one for one. The pin wording follows the strip's side, as in the
    // tab menu.
    const StripPosition position = state_.settings.position;
    const bool side = position == StripPosition::left || position == StripPosition::right;
    for (const int id : {IDC_CLICK_ACTIVE, IDC_DBLCLICK_TAB, IDC_MIDDLE}) {
        fill_combo(control(id), {L"None", L"Show now playing", L"Jump to the first / last track", L"Rename playlist",
                                 L"Duplicate playlist", side ? L"Pin to the top" : L"Pin to the left",
                                 side ? L"Pin to the bottom" : L"Pin to the right", L"Hide tab", L"Remove playlist",
                                 L"Lock / unlock playlist"});
    }
    // Order matters: DropName.
    fill_combo(control(IDC_DROP_NAME), {L"Creates a playlist named after the folder",
                                        L"Creates a playlist named New Playlist (n)"});
    if (const HWND examples = control(IDC_TITLE_EXAMPLES); examples != nullptr) {
        for (const wchar_t* example : title_examples) {
            ::SendMessageW(examples, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(example));
        }
        ::SendMessageW(examples, CB_SETCUEBANNER, 0, reinterpret_cast<LPARAM>(L"Pick one to use it"));
    }

    settings_to_controls();
    update_preview();

    return TRUE;
}

void ConfigureDialog::create_pages() {
    RECT host{};
    ::GetWindowRect(::GetDlgItem(m_hWnd, IDC_PAGE_HOST), &host);
    ::MapWindowPoints(nullptr, m_hWnd, reinterpret_cast<POINT*>(&host), 2);
    for (int i = 0; i < page_count; ++i) {
        const HWND page = ::CreateDialogParamW(core_api::get_my_instance(), MAKEINTRESOURCEW(IDD_PAGE_STRIP + i),
                                               m_hWnd, &ConfigureDialog::page_proc, 0);
        pages_[i] = page;
        if (page == nullptr) continue;
        ::SetWindowPos(page, ::GetDlgItem(m_hWnd, IDC_PAGE_HOST), host.left, host.top, host.right - host.left,
                       host.bottom - host.top, SWP_NOACTIVATE);
        dark_.AddDialogWithControls(page);
        pad_text_fields(page);
    }
}

INT_PTR CALLBACK ConfigureDialog::page_proc(HWND page, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_INITDIALOG: return FALSE;
    case WM_COMMAND:
    case WM_DRAWITEM:
    case WM_HSCROLL: ::SendMessageW(::GetParent(page), message, wparam, lparam); return TRUE;
    default: return FALSE;
    }
}

LRESULT ConfigureDialog::on_notify(UINT, WPARAM, LPARAM lparam) {
    SetMsgHandled(FALSE);
    if (lparam < 0x10000) return 0;
    const auto* header = reinterpret_cast<const NMHDR*>(lparam);
    if (header->idFrom == IDC_TABS && header->code == TCN_SELCHANGE) {
        show_page(static_cast<int>(::SendMessageW(header->hwndFrom, TCM_GETCURSEL, 0, 0)));
        SetMsgHandled(TRUE);
    }
    return 0;
}

void ConfigureDialog::show_page(int page) {
    for (int i = 0; i < page_count; ++i) {
        if (pages_[i] != nullptr) ::ShowWindow(pages_[i], i == page ? SW_SHOWNA : SW_HIDE);
    }
}

std::uint16_t ConfigureDialog::number(int id) const {
    BOOL ok = FALSE;
    const UINT value = ::GetDlgItemInt(::GetParent(control(id)), id, &ok, FALSE);
    return static_cast<std::uint16_t>(ok ? (std::min)(value, 0xFFFFu) : 0u);
}

void ConfigureDialog::set_slider(int id, int lo, int hi, int value) const {
    const HWND bar = control(id);
    ::SendMessageW(bar, TBM_SETRANGE, TRUE, MAKELPARAM(lo, hi));
    ::SendMessageW(bar, TBM_SETPAGESIZE, 0, 5);
    ::SendMessageW(bar, TBM_SETPOS, TRUE, value);
}

void ConfigureDialog::settings_to_controls() {
    loading_ = true;
    const Settings& s = state_.settings;
    select(IDC_POSITION, static_cast<int>(s.position));
    select(IDC_VISIBILITY, s.visibility == StripVisibility::always        ? 0
                           : s.visibility == StripVisibility::two_or_more ? 1
                           : s.visibility == StripVisibility::auto_hide   ? 2
                                                                          : 3);
    check(IDC_ROTATE, s.side_text == SideText::rotated);
    select(IDC_SIZING, static_cast<int>(s.sizing));
    select(IDC_ALIGN, static_cast<int>(s.align));
    select(IDC_CHEVRON, static_cast<int>(s.chevron_position));
    check(IDC_SHRINK, s.shrink_titles);
    set_number(IDC_PAD_X, s.pad_x);
    set_number(IDC_PAD_Y, s.pad_y);
    set_number(IDC_SPACING, s.spacing);
    set_number(IDC_THICKNESS, s.thickness);
    set_number(IDC_MAX_WIDTH, s.max_tab_width);

    select(IDC_INDICATOR, static_cast<int>(s.indicator));
    set_number(IDC_LINE_WIDTH, s.line_width);
    check(IDC_CHIP, s.chip);
    set_number(IDC_RADIUS, s.corner_radius);
    check(IDC_STRENGTH_AUTO, s.accent_strength == 0);
    set_slider(IDC_STRENGTH, 5, 100, s.accent_strength == 0 ? 35 : s.accent_strength);
    select(IDC_ACCENT_SOURCE, static_cast<int>(s.accent_source));
    ::SetWindowTextW(control(IDC_ACCENT_HEX), format_rgb(s.accent_argb).c_str());
    select(IDC_BACKGROUND, static_cast<int>(s.strip_background));
    ::SetWindowTextW(control(IDC_BACKGROUND_HEX), format_rgb(s.background_argb).c_str());
    set_slider(IDC_TINT, 2, 60, s.tint_strength);
    check(IDC_TRANSPARENT, s.transparent_background);

    hover_to_controls();
    select(IDC_HOVER_TEXT, static_cast<int>(s.hover_text));
    check(IDC_HOVER_LIGHTEN, s.active_hover_lighten);
    check(IDC_HOVER_FADE, s.hover_fade);
    set_number(IDC_HOVER_FADE_MS, s.hover_fade_ms);

    check(IDC_WHEEL, s.wheel_cycles);
    check(IDC_DRAG, s.drag_reorder);
    check(IDC_CTRL_TAB, s.ctrl_tab);
    select(IDC_CLICK_ACTIVE, static_cast<int>(s.click_active_action));
    select(IDC_MIDDLE, static_cast<int>(s.middle_action));
    check(IDC_UNPIN_PINNED, s.unpin_pinned);
    check(IDC_CONFIRM_REMOVE, s.confirm_remove);
    check(IDC_DBLCLICK_NEW, s.dblclick_new);
    select(IDC_DBLCLICK_TAB, static_cast<int>(s.dblclick_action));
    select(IDC_DROP_NAME, static_cast<int>(s.drop_name));
    check(IDC_FOLLOW_PLAYING, s.follow_playing);

    check(IDC_TITLE_NAME, s.title_mode == TitleMode::playlist_name);
    check(IDC_TITLE_FORMAT_MODE, s.title_mode == TitleMode::format);
    ::SetWindowTextW(control(IDC_TITLE_PATTERN), to_wide(s.title_format).c_str());
    check(IDC_SWITCH_ANIM, s.animations);
    set_number(IDC_SWITCH_MS, s.switch_ms);

    select(IDC_AH_MODE, static_cast<int>(s.reveal_mode));
    select(IDC_AH_ANIM, static_cast<int>(s.show_hide_animation));
    set_number(IDC_AH_ANIM_MS, s.animation_ms);
    set_number(IDC_AH_HOT_ZONE, s.hot_zone);
    set_number(IDC_AH_REVEAL, s.reveal_delay_ms);
    set_number(IDC_AH_HIDE, s.hide_delay_ms);
    set_number(IDC_AH_LINGER, s.linger_ms);
    show_fonts();
    loading_ = false;
    update_values();
    update_enabled();
    ::InvalidateRect(control(IDC_ACCENT_SWATCH), nullptr, FALSE);
    ::InvalidateRect(control(IDC_BACKGROUND_SWATCH), nullptr, FALSE);
    ::InvalidateRect(control(IDC_HOVER_SWATCH), nullptr, FALSE);
}

void ConfigureDialog::fill_hover_styles() {
    std::vector<const wchar_t*> names;
    if (hover_active_) names.push_back(L"Plain wash");
    // Order matters: HoverStyle, one for one (see hover_style_index).
    for (const wchar_t* name : {L"Fill", L"Outline", L"Outline and fill", L"Underline", L"Underline and fill", L"No mark"}) {
        names.push_back(name);
    }
    const HWND combo = control(IDC_HOVER_STYLE);
    ::SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const wchar_t* name : names) ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
}

//! The controls the two hover sets share, from the set the page shows.
void ConfigureDialog::hover_to_controls() {
    const Settings& s = state_.settings;
    const HoverFields& h = hover_fields();
    select(IDC_HOVER_STYLE, hover_style_index(s.*h.style, hover_active_));
    select(IDC_HOVER_COLOUR, static_cast<int>(s.*h.colour));
    ::SetWindowTextW(control(IDC_HOVER_HEX), format_rgb(s.*h.argb).c_str());
    check(IDC_HOVER_FILL_AUTO, s.*h.fill_strength == 0);
    set_slider(IDC_HOVER_FILL, 2, 80, s.*h.fill_strength == 0 ? 10 : s.*h.fill_strength);
    set_number(IDC_HOVER_LINE_WIDTH, s.*h.line_width);
    check(IDC_HOVER_LINE_AUTO, s.*h.line_opacity == 0);
    set_slider(IDC_HOVER_LINE, 10, 100, s.*h.line_opacity == 0 ? 50 : s.*h.line_opacity);
    // The title row: the others' choice of colour, or the active tab's lighten box.
    ::ShowWindow(control(IDC_HOVER_TEXT), hover_active_ ? SW_HIDE : SW_SHOWNA);
    ::ShowWindow(control(IDC_HOVER_LIGHTEN), hover_active_ ? SW_SHOWNA : SW_HIDE);
    ::InvalidateRect(control(IDC_HOVER_SWATCH), nullptr, FALSE);
}

void ConfigureDialog::settings_from_controls() {
    Settings& s = state_.settings;
    const auto pick = [&](int id, auto& field) {
        const LRESULT index = selection(id);
        if (index != CB_ERR) field = static_cast<std::remove_reference_t<decltype(field)>>(index);
    };
    pick(IDC_POSITION, s.position);
    if (const LRESULT v = selection(IDC_VISIBILITY); v != CB_ERR) {
        constexpr StripVisibility visibility_order[] = {StripVisibility::always, StripVisibility::two_or_more,
                                                        StripVisibility::auto_hide, StripVisibility::never};
        if (v >= 0 && v < 4) s.visibility = visibility_order[v];
    }
    s.side_text = checked(IDC_ROTATE) ? SideText::rotated : SideText::horizontal;
    pick(IDC_SIZING, s.sizing);
    pick(IDC_ALIGN, s.align);
    pick(IDC_CHEVRON, s.chevron_position);
    s.shrink_titles = checked(IDC_SHRINK);
    s.pad_x = number(IDC_PAD_X);
    s.pad_y = number(IDC_PAD_Y);
    s.spacing = number(IDC_SPACING);
    s.thickness = number(IDC_THICKNESS);
    s.max_tab_width = number(IDC_MAX_WIDTH);

    pick(IDC_INDICATOR, s.indicator);
    s.line_width = static_cast<std::uint8_t>((std::min)(number(IDC_LINE_WIDTH), std::uint16_t{8}));
    s.chip = checked(IDC_CHIP);
    s.corner_radius = number(IDC_RADIUS);
    s.accent_strength = checked(IDC_STRENGTH_AUTO) ? 0 : static_cast<std::uint8_t>(std::clamp(slider(IDC_STRENGTH), 5, 100));
    pick(IDC_ACCENT_SOURCE, s.accent_source);
    s.accent_argb = parse_rgb(window_text(control(IDC_ACCENT_HEX)), s.accent_argb);
    pick(IDC_BACKGROUND, s.strip_background);
    s.background_argb = parse_rgb(window_text(control(IDC_BACKGROUND_HEX)), s.background_argb);
    s.tint_strength = static_cast<std::uint8_t>(std::clamp(slider(IDC_TINT), 2, 60));
    s.transparent_background = checked(IDC_TRANSPARENT);

    const HoverFields& h = hover_fields();
    if (const LRESULT index = selection(IDC_HOVER_STYLE); index != CB_ERR) s.*h.style = hover_style_at(index, hover_active_);
    pick(IDC_HOVER_COLOUR, s.*h.colour);
    s.*h.argb = parse_rgb(window_text(control(IDC_HOVER_HEX)), s.*h.argb);
    s.*h.fill_strength =
        checked(IDC_HOVER_FILL_AUTO) ? 0 : static_cast<std::uint8_t>(std::clamp(slider(IDC_HOVER_FILL), 2, 80));
    s.*h.line_width = static_cast<std::uint8_t>((std::min)(number(IDC_HOVER_LINE_WIDTH), std::uint16_t{8}));
    s.*h.line_opacity =
        checked(IDC_HOVER_LINE_AUTO) ? 0 : static_cast<std::uint8_t>(std::clamp(slider(IDC_HOVER_LINE), 10, 100));
    pick(IDC_HOVER_TEXT, s.hover_text);
    s.active_hover_lighten = checked(IDC_HOVER_LIGHTEN);
    s.hover_fade = checked(IDC_HOVER_FADE);
    s.hover_fade_ms = number(IDC_HOVER_FADE_MS);

    s.wheel_cycles = checked(IDC_WHEEL);
    s.drag_reorder = checked(IDC_DRAG);
    s.ctrl_tab = checked(IDC_CTRL_TAB);
    pick(IDC_CLICK_ACTIVE, s.click_active_action);
    pick(IDC_MIDDLE, s.middle_action);
    s.unpin_pinned = checked(IDC_UNPIN_PINNED);
    s.confirm_remove = checked(IDC_CONFIRM_REMOVE);
    s.dblclick_new = checked(IDC_DBLCLICK_NEW);
    pick(IDC_DBLCLICK_TAB, s.dblclick_action);
    pick(IDC_DROP_NAME, s.drop_name);
    s.follow_playing = checked(IDC_FOLLOW_PLAYING);

    s.title_mode = checked(IDC_TITLE_FORMAT_MODE) ? TitleMode::format : TitleMode::playlist_name;
    s.title_format = to_utf8(window_text(control(IDC_TITLE_PATTERN)));
    s.animations = checked(IDC_SWITCH_ANIM);
    s.switch_ms = number(IDC_SWITCH_MS);

    pick(IDC_AH_MODE, s.reveal_mode);
    pick(IDC_AH_ANIM, s.show_hide_animation);
    s.animation_ms = number(IDC_AH_ANIM_MS);
    s.hot_zone = number(IDC_AH_HOT_ZONE);
    s.reveal_delay_ms = number(IDC_AH_REVEAL);
    s.hide_delay_ms = number(IDC_AH_HIDE);
    s.linger_ms = number(IDC_AH_LINGER);
}

void ConfigureDialog::update_values() {
    wchar_t text[16]{};
    std::swprintf(text, 16, L"%d%%", slider(IDC_STRENGTH));
    set_label(IDC_STRENGTH_VALUE, checked(IDC_STRENGTH_AUTO) ? L"" : text);
    std::swprintf(text, 16, L"%d%%", slider(IDC_TINT));
    set_label(IDC_TINT_VALUE, text);
    std::swprintf(text, 16, L"%d%%", slider(IDC_HOVER_FILL));
    set_label(IDC_HOVER_FILL_VALUE, checked(IDC_HOVER_FILL_AUTO) ? L"" : text);
    std::swprintf(text, 16, L"%d%%", slider(IDC_HOVER_LINE));
    set_label(IDC_HOVER_LINE_VALUE, checked(IDC_HOVER_LINE_AUTO) ? L"" : text);
    update_preview();
}

void ConfigureDialog::update_preview() {
    const Settings& s = state_.settings;
    const bool format = s.title_mode == TitleMode::format && !s.title_format.empty();
    std::wstring title = playlists::preview_title(format ? s.title_format : std::string());
    std::wstring text = L"Active playlist: ";
    text += title.empty() ? std::wstring(L"(empty)") : title;
    ::SetWindowTextW(control(IDC_TITLE_PREVIEW), text.c_str());
}

void ConfigureDialog::show_fonts() {
    const TabFont& f = state_.settings.font;
    const std::string text =
        f.family.empty()
            ? fbc::fonts::describe_default({fbc::fonts::narrow(state_.host_font_family), state_.host_font_tenths})
            : fbc::fonts::describe(font_choice(f));
    set_label(IDC_FONT_TEXT, fbc::fonts::widen(text).c_str());
    for (int i = 0; i < static_cast<int>(f.fallbacks.size()); ++i) {
        const std::string& family = f.fallbacks[static_cast<std::size_t>(i)];
        set_label(IDC_FALLBACK_TEXT + i, family.empty() ? L"None" : fbc::fonts::widen(family).c_str());
    }
}

bool ConfigureDialog::on_font_command(int id) {
    TabFont& f = state_.settings.font;
    const int slots = static_cast<int>(f.fallbacks.size());
    const std::string host = fbc::fonts::narrow(state_.host_font_family);
    if (id == IDC_FONT_PICK) {
        fbc::fonts::FontChoice choice = font_choice(f);
        if (!fbc::fonts::pick_font(m_hWnd, choice, {host, state_.host_font_tenths, 400})) return true;
        f.family = choice.family;
        f.tenths_pt = static_cast<std::uint16_t>((std::min)(choice.tenths_pt, 720u));
        f.weight = static_cast<std::uint16_t>((std::min)(choice.weight, 1000u));
        f.italic = choice.italic;
    } else if (id == IDC_FONT_CLEAR) {
        f.family.clear();
        f.tenths_pt = 0;
        f.weight = 0;
        f.italic = false;
    } else if (id >= IDC_FALLBACK_PICK && id < IDC_FALLBACK_PICK + slots) {
        std::string& slot = f.fallbacks[static_cast<std::size_t>(id - IDC_FALLBACK_PICK)];
        if (!fbc::fonts::pick_family(m_hWnd, slot, f.family.empty() ? host : f.family)) return true;
    } else if (id >= IDC_FALLBACK_CLEAR && id < IDC_FALLBACK_CLEAR + slots) {
        f.fallbacks[static_cast<std::size_t>(id - IDC_FALLBACK_CLEAR)].clear();
    } else {
        return false;
    }
    show_fonts();
    changed();
    return true;
}

void ConfigureDialog::update_enabled() {
    const Settings& s = state_.settings;
    enable(IDC_ROTATE, s.position == StripPosition::left || s.position == StripPosition::right);
    enable(IDC_ALIGN, s.sizing != TabSizing::fill);
    // The strength is the opacity of the accent fill, which only the pill and the tab have.
    const bool fill = s.indicator == Indicator::pill || s.indicator == Indicator::tab;
    enable(IDC_STRENGTH_AUTO, fill);
    enable(IDC_LINE_WIDTH, s.indicator == Indicator::underline || s.indicator == Indicator::tab_outline);
    enable(IDC_STRENGTH, fill && s.accent_strength != 0);
    enable(IDC_STRENGTH_VALUE, fill && s.accent_strength != 0);
    const bool custom_accent = s.accent_source == AccentSource::custom;
    enable(IDC_ACCENT_HEX, custom_accent);
    enable(IDC_ACCENT_SWATCH, custom_accent);
    const bool custom_background = s.strip_background == StripBackground::custom;
    enable(IDC_BACKGROUND_HEX, custom_background);
    enable(IDC_BACKGROUND_SWATCH, custom_background);
    const bool tint = s.strip_background == StripBackground::accent_tint;
    enable(IDC_TINT, tint);
    enable(IDC_TINT_VALUE, tint);

    // Hover: each control only where the chosen style uses it.
    const HoverFields& h = hover_fields();
    const HoverStyle hs = s.*h.style;
    const bool hover_fill = hs == HoverStyle::fill || hs == HoverStyle::outline_fill || hs == HoverStyle::underline_fill;
    const bool hover_line = hs == HoverStyle::outline || hs == HoverStyle::outline_fill ||
                            hs == HoverStyle::underline || hs == HoverStyle::underline_fill;
    // The plain wash is always the text colour.
    const bool hover_tinted = (hs != HoverStyle::none && hs != HoverStyle::plain) ||
                              (!hover_active_ && s.hover_text == HoverText::colour);
    enable(IDC_HOVER_COLOUR, hover_tinted);
    const bool custom_hover = hover_tinted && s.*h.colour == HoverColour::custom;
    enable(IDC_HOVER_HEX, custom_hover);
    enable(IDC_HOVER_SWATCH, custom_hover);
    enable(IDC_HOVER_FILL_AUTO, hover_fill);
    enable(IDC_HOVER_FILL, hover_fill && s.*h.fill_strength != 0);
    enable(IDC_HOVER_FILL_VALUE, hover_fill && s.*h.fill_strength != 0);
    enable(IDC_HOVER_LINE_WIDTH, hover_line);
    enable(IDC_HOVER_LINE_AUTO, hover_line);
    enable(IDC_HOVER_LINE, hover_line && s.*h.line_opacity != 0);
    enable(IDC_HOVER_LINE_VALUE, hover_line && s.*h.line_opacity != 0);
    // The fade is shared: it matters while either set shows something.
    const bool fades = s.hover_style != HoverStyle::none || s.hover_text != HoverText::unchanged ||
                       s.active_hover_style != HoverStyle::none || s.active_hover_lighten;
    enable(IDC_HOVER_FADE, fades);
    enable(IDC_HOVER_FADE_MS, s.hover_fade && fades);

    enable(IDC_SWITCH_MS, s.animations);
    const auto any_action = [&s](TabAction a, TabAction b = TabAction::none) {
        for (const TabAction action : {s.click_active_action, s.dblclick_action, s.middle_action}) {
            if (action == a || (b != TabAction::none && action == b)) return true;
        }
        return false;
    };
    enable(IDC_CONFIRM_REMOVE, any_action(TabAction::remove_playlist));
    enable(IDC_UNPIN_PINNED, any_action(TabAction::pin_start, TabAction::pin_end));

    const bool auto_hide = s.visibility == StripVisibility::auto_hide;
    for (const int id : {IDC_AH_MODE, IDC_AH_ANIM, IDC_AH_HOT_ZONE, IDC_AH_REVEAL, IDC_AH_HIDE, IDC_AH_LINGER}) {
        enable(id, auto_hide);
    }
    // Show/hide animations run on the layered strip, i.e. over the panel only.
    enable(IDC_AH_ANIM, auto_hide && s.reveal_mode == RevealMode::overlay);
    enable(IDC_AH_ANIM_MS, auto_hide && s.reveal_mode == RevealMode::overlay &&
                               s.show_hide_animation != ShowHideAnimation::none);

    enable(IDC_TITLE_PATTERN, s.title_mode == TitleMode::format);

}


void ConfigureDialog::changed() {
    update_values();
    update_enabled();
    if (live_) target_.preview(state_);
}

void ConfigureDialog::on_command(UINT code, int id, CWindow) {
    switch (id) {
    case IDOK:
        settings_from_controls();
        EndDialog(IDOK);
        return;
    case IDCANCEL: EndDialog(IDCANCEL); return;
    default: break;
    }
    if (loading_) return;
    if (code == BN_CLICKED && on_font_command(id)) return;
    switch (id) {
    case IDC_DEFAULTS:
        state_.settings = Settings{};
        settings_to_controls();
        changed();
        return;
    case IDC_ACCENT_SWATCH:
    case IDC_BACKGROUND_SWATCH:
    case IDC_HOVER_SWATCH: {
        if (code != BN_CLICKED) return;
        const Swatch& swatch = swatch_for(id);
        const int hex = swatch.hex;
        std::uint32_t argb = state_.settings.*swatch_field(swatch);
        // Fires EN_CHANGE, and with it the usual change handling.
        if (pick_colour(m_hWnd, argb)) ::SetWindowTextW(control(hex), format_rgb(argb).c_str());
        return;
    }
    case IDC_HOVER_TARGET: {
        // Another set of settings, not a change: show it without reading the controls first.
        if (code != CBN_SELCHANGE) return;
        const bool active = selection(IDC_HOVER_TARGET) == 1;
        if (active == hover_active_) return;
        hover_active_ = active;
        loading_ = true;
        fill_hover_styles();
        hover_to_controls();
        loading_ = false;
        update_values();
        update_enabled();
        return;
    }
    case IDC_TITLE_HELP:
        if (code == BN_CLICKED) open_titleformat_help(m_hWnd);
        return;
    case IDC_TITLE_EXAMPLES: {
        if (code != CBN_SELCHANGE) return;
        const HWND examples = control(IDC_TITLE_EXAMPLES);
        const LRESULT pick_index = ::SendMessageW(examples, CB_GETCURSEL, 0, 0);
        if (pick_index < 0 || static_cast<std::size_t>(pick_index) >= std::size(title_examples)) return;
        // A menu, not a setting: the list goes back to its cue text.
        ::SendMessageW(examples, CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
        check(IDC_TITLE_NAME, false);
        check(IDC_TITLE_FORMAT_MODE, true);
        ::SetWindowTextW(control(IDC_TITLE_PATTERN), title_examples[pick_index]); // EN_CHANGE applies it
        settings_from_controls();
        changed();
        return;
    }
    default: break;
    }
    if (code == EN_CHANGE || code == BN_CLICKED || code == CBN_SELCHANGE) {
        settings_from_controls();
        if (id == IDC_ACCENT_HEX) ::InvalidateRect(control(IDC_ACCENT_SWATCH), nullptr, FALSE);
        if (id == IDC_BACKGROUND_HEX) ::InvalidateRect(control(IDC_BACKGROUND_SWATCH), nullptr, FALSE);
        if (id == IDC_HOVER_HEX) ::InvalidateRect(control(IDC_HOVER_SWATCH), nullptr, FALSE);
        changed();
    }
}

LRESULT ConfigureDialog::on_hscroll(UINT, WPARAM, LPARAM lparam) {
    const auto bar = reinterpret_cast<HWND>(lparam);
    if (loading_ || bar == nullptr ||
        (bar != control(IDC_STRENGTH) && bar != control(IDC_TINT) && bar != control(IDC_HOVER_FILL) &&
         bar != control(IDC_HOVER_LINE))) {
        SetMsgHandled(FALSE);
        return 0;
    }
    settings_from_controls();
    changed();
    return 0;
}

void ConfigureDialog::on_draw_item(UINT, LPDRAWITEMSTRUCT item) {
    if (item == nullptr || (item->CtlID != IDC_ACCENT_SWATCH && item->CtlID != IDC_BACKGROUND_SWATCH &&
                            item->CtlID != IDC_HOVER_SWATCH)) {
        SetMsgHandled(FALSE);
        return;
    }
    const Swatch& swatch = swatch_for(static_cast<int>(item->CtlID));
    const std::uint32_t argb = parse_rgb(window_text(control(swatch.hex)), state_.settings.*swatch_field(swatch));
    draw_swatch(*item, colorref(argb));
}

// ---------------------------------------------------------------------------------------------

class RenameDialog : public CDialogImpl<RenameDialog> {
public:
    enum { IDD = IDD_RENAME };

    explicit RenameDialog(std::wstring& name) : name_(name) {}

    BEGIN_MSG_MAP_EX(RenameDialog)
        MSG_WM_INITDIALOG(on_init_dialog)
        COMMAND_ID_HANDLER_EX(IDOK, on_ok)
        COMMAND_ID_HANDLER_EX(IDCANCEL, on_cancel)
    END_MSG_MAP()

private:
    BOOL on_init_dialog(CWindow, LPARAM) {
        dark_.AddDialogWithControls(*this);
        pad_text_fields(m_hWnd);
        const HWND edit = GetDlgItem(IDC_RENAME_TITLE);
        ::SetWindowTextW(edit, name_.c_str());
        ::SendMessageW(edit, EM_SETSEL, 0, -1);
        ::SetFocus(edit);
        return FALSE; // focus set here
    }
    void on_ok(UINT, int, CWindow) {
        name_ = window_text(GetDlgItem(IDC_RENAME_TITLE));
        EndDialog(IDOK);
    }
    void on_cancel(UINT, int, CWindow) { EndDialog(IDCANCEL); }

    std::wstring& name_;
    fb2k::CDarkModeHooks dark_;
};

// A Yes/No question in foobar2000's theme (MessageBox cannot be dark).
class ConfirmDialog : public CDialogImpl<ConfirmDialog> {
public:
    enum { IDD = IDD_CONFIRM };

    ConfirmDialog(const std::wstring& title, const std::wstring& text) : title_(title), text_(text) {}

    BEGIN_MSG_MAP_EX(ConfirmDialog)
        MSG_WM_INITDIALOG(on_init_dialog)
        COMMAND_ID_HANDLER_EX(IDYES, on_answer)
        COMMAND_ID_HANDLER_EX(IDNO, on_answer)
        COMMAND_ID_HANDLER_EX(IDCANCEL, on_answer)
    END_MSG_MAP()

private:
    BOOL on_init_dialog(CWindow, LPARAM) {
        dark_.AddDialogWithControls(*this);
        SetWindowTextW(title_.c_str());
        ::SetWindowTextW(GetDlgItem(IDC_CONFIRM_TEXT), text_.c_str());
        ::SendMessageW(GetDlgItem(IDC_CONFIRM_ICON), STM_SETICON,
                       reinterpret_cast<WPARAM>(::LoadIconW(nullptr, IDI_QUESTION)), 0);
        ::MessageBeep(MB_ICONQUESTION);
        return TRUE; // focus on the default button (Yes)
    }
    void on_answer(UINT, int id, CWindow) { EndDialog(id == IDYES ? IDYES : IDNO); }

    std::wstring title_;
    std::wstring text_;
    fb2k::CDarkModeHooks dark_;
};

} // namespace

bool run_configure_dialog(HWND parent, ConfigureState& state, ConfigureTarget& target, bool live) {
    ConfigureDialog dialog(state, target, live);
    return dialog.DoModal(parent) == IDOK;
}

bool run_confirm_dialog(HWND parent, const std::wstring& title, const std::wstring& text) {
    ConfirmDialog dialog(title, text);
    return dialog.DoModal(parent) == IDYES;
}

bool run_rename_dialog(HWND parent, std::wstring& name) {
    RenameDialog dialog(name);
    return dialog.DoModal(parent) == IDOK;
}

} // namespace ept
