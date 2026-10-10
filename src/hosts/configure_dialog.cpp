// The Configure dialog and the Rename dialog. Layout: foo_enhancedplaylisttabs.rc (style profile at the top);
// conventions: one child dialog per
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

constexpr int page_count = 10;
//! In tab order, with their tab names.
constexpr int page_ids[page_count] = {IDD_PAGE_STRIP,     IDD_PAGE_LOOK,      IDD_PAGE_HOVER, IDD_PAGE_COLOURS,
                                      IDD_PAGE_FONTS,     IDD_PAGE_TITLES,    IDD_PAGE_ANIMATION,
                                      IDD_PAGE_PLAYLISTS, IDD_PAGE_INPUT,     IDD_PAGE_VISIBILITY};
constexpr const wchar_t* page_names[page_count] = {L"Strip",     L"Look",  L"Hover",     L"Colours",
                                                   L"Fonts",     L"Titles", L"Animation", L"Playlists",
                                                   L"Input",     L"Visibility"};
//! Where an automatic line width's edit rests (the width it draws at 96 DPI).
[[nodiscard]] int auto_line_width(bool outline) noexcept { return outline ? 1 : 2; }

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
    L"$if(%isplaying%,\u25B6 )%title%",
    L"$if(%isplaying%,$if(%ispaused%,'\u275A\u275A ','\u25B6 '))%title%",
    L"$if(%isplaying%,$if(%ispaused%,'\u275A\u275A ','\u25B6 '))$if(%is_locked%,\U0001F512 )%title%",
    L"$if(%is_locked%,\U0001F512 )%title%",
    L"$if(%is_locked%,'['%lock_name%']' )%title%",
    L"$if(%playlist_queue_total%,'['%playlist_queue_total%/%queue_total%']' )%title%",
    L"$if(%is_active%,%title% \u00B7 %size%,%title%)",
    L"$left(%title%,16)",
    L"$upper(%title%)",
};

//! Widens a drop-down's list (not the box) so its longest item shows in full, up to the work area.
void fit_dropped_width(HWND combo) {
    if (combo == nullptr) return;
    const HDC dc = ::GetDC(combo);
    if (dc == nullptr) return;
    const HGDIOBJ old = ::SelectObject(dc, reinterpret_cast<HFONT>(::SendMessageW(combo, WM_GETFONT, 0, 0)));
    int widest = 0;
    const LRESULT count = ::SendMessageW(combo, CB_GETCOUNT, 0, 0);
    std::wstring text;
    for (LRESULT i = 0; i < count; ++i) {
        const LRESULT length = ::SendMessageW(combo, CB_GETLBTEXTLEN, static_cast<WPARAM>(i), 0);
        if (length <= 0) continue;
        text.resize(static_cast<std::size_t>(length) + 1);
        ::SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(text.data()));
        SIZE size{};
        if (::GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(length), &size) != FALSE) {
            widest = (std::max)(widest, static_cast<int>(size.cx));
        }
    }
    ::SelectObject(dc, old);
    ::ReleaseDC(combo, dc);
    RECT box{};
    ::GetWindowRect(combo, &box);
    int width = widest + ::GetSystemMetrics(SM_CXVSCROLL) + 4 * ::GetSystemMetrics(SM_CXEDGE) + 8;
    MONITORINFO mi{sizeof mi};
    if (::GetMonitorInfoW(::MonitorFromWindow(combo, MONITOR_DEFAULTTONEAREST), &mi) != FALSE) {
        width = (std::min)(width, static_cast<int>(mi.rcWork.right - mi.rcWork.left));
    }
    if (width > box.right - box.left) ::SendMessageW(combo, CB_SETDROPPEDWIDTH, static_cast<WPARAM>(width), 0);
}

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
    {IDC_HOVER_TEXT_SWATCH, IDC_HOVER_TEXT_HEX, &Settings::hover_text_argb},
    {IDC_HOVER_ACTIVE_SWATCH, IDC_HOVER_ACTIVE_HEX, &Settings::active_hover_argb},
    {IDC_HOVER_ACTIVE_TEXT_SWATCH, IDC_HOVER_ACTIVE_TEXT_HEX, &Settings::active_hover_text_argb},
    {IDC_TEXT_SWATCH, IDC_TEXT_HEX, &Settings::text_argb},
    {IDC_ACTIVE_TEXT_SWATCH, IDC_ACTIVE_TEXT_HEX, &Settings::active_text_argb},
    {IDC_CHIP_SWATCH, IDC_CHIP_HEX, &Settings::chip_argb},
};
//! The Hover page shows two sets side by side: the other tabs' (Settings::hover_*) and the
//! active tab's (Settings::active_hover_*).
struct HoverFields {
    HoverStyle Settings::*style;
    HoverColour Settings::*colour;
    std::uint32_t Settings::*argb;
    std::uint8_t Settings::*fill_strength;
    std::uint8_t Settings::*line_width;
    std::uint8_t Settings::*line_opacity;
    HoverText Settings::*text;
    std::uint32_t Settings::*text_argb;
};
constexpr HoverFields hover_others{&Settings::hover_style,         &Settings::hover_colour,
                                   &Settings::hover_argb,          &Settings::hover_fill_strength,
                                   &Settings::hover_line_width,    &Settings::hover_line_opacity,
                                   &Settings::hover_text,          &Settings::hover_text_argb};
constexpr HoverFields hover_active{&Settings::active_hover_style,         &Settings::active_hover_colour,
                                   &Settings::active_hover_argb,          &Settings::active_hover_fill_strength,
                                   &Settings::active_hover_line_width,    &Settings::active_hover_line_opacity,
                                   &Settings::active_hover_text,          &Settings::active_hover_text_argb};
//! One column of the Hover page.
struct HoverIds {
    int style, colour, hex, swatch, text, text_hex, text_swatch;
    int fill_auto, fill, fill_value, line_width_auto, line_width, line_auto, line, line_value;
};
struct HoverSet {
    const HoverFields& fields;
    HoverIds ids;
    bool active;
};
constexpr HoverSet hover_sets[] = {
    {hover_others,
     {IDC_HOVER_STYLE, IDC_HOVER_COLOUR, IDC_HOVER_HEX, IDC_HOVER_SWATCH, IDC_HOVER_TEXT, IDC_HOVER_TEXT_HEX,
      IDC_HOVER_TEXT_SWATCH, IDC_HOVER_FILL_AUTO, IDC_HOVER_FILL, IDC_HOVER_FILL_VALUE, IDC_HOVER_LINE_WIDTH_AUTO, IDC_HOVER_LINE_WIDTH,
      IDC_HOVER_LINE_AUTO, IDC_HOVER_LINE, IDC_HOVER_LINE_VALUE},
     false},
    {hover_active,
     {IDC_HOVER_ACTIVE_STYLE, IDC_HOVER_ACTIVE_COLOUR, IDC_HOVER_ACTIVE_HEX, IDC_HOVER_ACTIVE_SWATCH,
      IDC_HOVER_ACTIVE_TEXT, IDC_HOVER_ACTIVE_TEXT_HEX, IDC_HOVER_ACTIVE_TEXT_SWATCH, IDC_HOVER_ACTIVE_FILL_AUTO,
      IDC_HOVER_ACTIVE_FILL, IDC_HOVER_ACTIVE_FILL_VALUE, IDC_HOVER_ACTIVE_LINE_WIDTH_AUTO, IDC_HOVER_ACTIVE_LINE_WIDTH, IDC_HOVER_ACTIVE_LINE_AUTO,
      IDC_HOVER_ACTIVE_LINE, IDC_HOVER_ACTIVE_LINE_VALUE},
     true},
};
//! The style list: the HoverStyle values in order; the active tab's starts with its plain wash.
//! The hover mark's line is an outline (else an underline).
[[nodiscard]] bool hover_outline(HoverStyle style) noexcept {
    return style == HoverStyle::outline || style == HoverStyle::outline_fill;
}
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

    //! Modal (DoModal): edits `state` in place.
    ConfigureDialog(ConfigureState& state, ConfigureTarget& target, bool live)
        : state_(state), target_(target), live_(live), modeless_(false) {}
    //! Modeless (open_configure_dialog): edits its own copy, always live, and tells `target` how it
    //! closed. Deletes itself.
    ConfigureDialog(const ConfigureState& state, ConfigureTarget& target)
        : own_(state), state_(own_), target_(target), live_(true), modeless_(true) {}

    //! Closes a modeless dialog without telling the target.
    static constexpr UINT wm_close_quietly = WM_APP + 1;

    BEGIN_MSG_MAP_EX(ConfigureDialog)
        MSG_WM_INITDIALOG(on_init_dialog)
        MSG_WM_DESTROY(on_destroy)
        MESSAGE_HANDLER_EX(wm_close_quietly, on_close_quietly)
        MSG_WM_COMMAND(on_command)
        MSG_WM_DRAWITEM(on_draw_item)
        MESSAGE_HANDLER_EX(WM_HSCROLL, on_hscroll)
        MESSAGE_HANDLER_EX(WM_NOTIFY, on_notify)
    END_MSG_MAP()

    void OnFinalMessage(HWND) override {
        if (modeless_) delete this;
    }

private:
    BOOL on_init_dialog(CWindow, LPARAM);
    void on_destroy();
    LRESULT on_close_quietly(UINT, WPARAM, LPARAM);
    //! OK or Cancel: modal, ends the dialog; modeless, tells the target and destroys the window.
    void finish(bool ok);
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

    //! The modeless dialog's state (state_ refers to it); unused when modal.
    ConfigureState own_;
    ConfigureState& state_;
    ConfigureTarget& target_;
    const bool live_;
    const bool modeless_;
    //! The target knows how the modeless dialog closed (or must not hear of it).
    bool told_{false};
    bool loading_{false};
    void fill_hover_styles(const HoverSet& set);
    void hover_to_controls(const HoverSet& set);
    void hover_from_controls(const HoverSet& set);
    void hover_values(const HoverSet& set);
    void hover_enabled(const HoverSet& set);
    HWND pages_[page_count]{};
    // Must be a member: it hooks the dialog and its controls for the lifetime of both.
    fb2k::CDarkModeHooks dark_;
};

BOOL ConfigureDialog::on_init_dialog(CWindow, LPARAM) {
    dark_.AddDialogWithControls(*this);
    create_pages();
    {
        const HWND tabs = ::GetDlgItem(m_hWnd, IDC_TABS);
        for (int i = 0; i < page_count; ++i) {
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            item.pszText = const_cast<wchar_t*>(page_names[i]);
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
    // Order matters: ChipColour.
    fill_combo(control(IDC_CHIP_COLOUR), {L"Text colour", L"Accent colour", L"Custom colour"});
    for (const HoverSet& set : hover_sets) {
        fill_hover_styles(set);
        // Order matters: HoverColour and HoverText, one for one.
        fill_combo(control(set.ids.colour), {L"Text colour", L"Accent colour", L"Custom colour"});
        fill_combo(control(set.ids.text), {L"Brightens", L"Stays as it is", L"Takes the hover colour", L"Custom colour"});
    }
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
    // Order matters: TracksMenu.
    fill_combo(control(IDC_TRACKS_MENU), {L"Hidden", L"Items submenu at the end", L"Items... entry that opens it"});
    // Order matters: DropName.
    fill_combo(control(IDC_DROP_NAME), {L"Creates a playlist named after the folder",
                                        L"Creates a playlist named New Playlist (n)"});
    if (const HWND examples = control(IDC_TITLE_EXAMPLES); examples != nullptr) {
        for (const wchar_t* example : title_examples) {
            ::SendMessageW(examples, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(example));
        }
        ::SendMessageW(examples, CB_SETCUEBANNER, 0, reinterpret_cast<LPARAM>(L"Examples: pick one to use it"));
        fit_dropped_width(examples);
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
        const HWND page = ::CreateDialogParamW(core_api::get_my_instance(), MAKEINTRESOURCEW(page_ids[i]),
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
    check(IDC_LINE_WIDTH_AUTO, s.line_width == 0);
    set_number(IDC_LINE_WIDTH, s.line_width != 0 ? s.line_width : auto_line_width(false));
    check(IDC_CHIP, s.chip);
    select(IDC_CHIP_COLOUR, static_cast<int>(s.chip_colour));
    ::SetWindowTextW(control(IDC_CHIP_HEX), format_rgb(s.chip_argb).c_str());
    check(IDC_CHIP_STRENGTH_AUTO, s.chip_strength == 0);
    set_slider(IDC_CHIP_STRENGTH, 2, 100,
               s.chip_strength != 0 ? s.chip_strength : (state_.dark ? auto_chip_dark : auto_chip_light));
    set_number(IDC_RADIUS, s.corner_radius);
    check(IDC_STRENGTH_AUTO, s.accent_strength == 0);
    set_slider(IDC_STRENGTH, 2, 100,
               s.accent_strength != 0 ? s.accent_strength : (state_.dark ? auto_fill_dark : auto_fill_light));
    select(IDC_ACCENT_SOURCE, static_cast<int>(s.accent_source));
    ::SetWindowTextW(control(IDC_ACCENT_HEX), format_rgb(s.accent_argb).c_str());
    select(IDC_BACKGROUND, static_cast<int>(s.strip_background));
    ::SetWindowTextW(control(IDC_BACKGROUND_HEX), format_rgb(s.background_argb).c_str());
    set_slider(IDC_TINT, 2, 100, s.tint_strength);
    check(IDC_TRANSPARENT, s.transparent_background);
    set_slider(IDC_TRANSPARENT_OPACITY, 0, 100, s.transparent_opacity);
    check(IDC_TEXT_CUSTOM, s.custom_text);
    ::SetWindowTextW(control(IDC_TEXT_HEX), format_rgb(s.text_argb).c_str());
    check(IDC_ACTIVE_TEXT_CUSTOM, s.custom_active_text);
    ::SetWindowTextW(control(IDC_ACTIVE_TEXT_HEX), format_rgb(s.active_text_argb).c_str());

    for (const HoverSet& set : hover_sets) hover_to_controls(set);
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
    select(IDC_TRACKS_MENU, static_cast<int>(s.tracks_menu));
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
    for (const Swatch& swatch : swatches) ::InvalidateRect(control(swatch.button), nullptr, FALSE);
}

void ConfigureDialog::fill_hover_styles(const HoverSet& set) {
    std::vector<const wchar_t*> names;
    if (set.active) names.push_back(L"Plain wash");
    // Order matters: HoverStyle, one for one (see hover_style_index).
    for (const wchar_t* name : {L"Fill", L"Outline", L"Outline and fill", L"Underline", L"Underline and fill", L"No mark"}) {
        names.push_back(name);
    }
    const HWND combo = control(set.ids.style);
    ::SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const wchar_t* name : names) ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
}

void ConfigureDialog::hover_to_controls(const HoverSet& set) {
    const Settings& s = state_.settings;
    const HoverFields& h = set.fields;
    const HoverIds& id = set.ids;
    select(id.style, hover_style_index(s.*h.style, set.active));
    select(id.colour, static_cast<int>(s.*h.colour));
    ::SetWindowTextW(control(id.hex), format_rgb(s.*h.argb).c_str());
    check(id.fill_auto, s.*h.fill_strength == 0);
    set_slider(id.fill, 2, 100, s.*h.fill_strength == 0 ? 10 : s.*h.fill_strength);
    check(id.line_width_auto, s.*h.line_width == 0);
    set_number(id.line_width, s.*h.line_width != 0 ? s.*h.line_width : auto_line_width(hover_outline(s.*h.style)));
    check(id.line_auto, s.*h.line_opacity == 0);
    set_slider(id.line, 10, 100, s.*h.line_opacity == 0 ? 50 : s.*h.line_opacity);
    // "Brightens" is the full text colour for the others, lighter (towards white) for the active tab.
    select(id.text, static_cast<int>(s.*h.text));
    ::SetWindowTextW(control(id.text_hex), format_rgb(s.*h.text_argb).c_str());
}

void ConfigureDialog::hover_from_controls(const HoverSet& set) {
    Settings& s = state_.settings;
    const HoverFields& h = set.fields;
    const HoverIds& id = set.ids;
    if (const LRESULT index = selection(id.style); index != CB_ERR) s.*h.style = hover_style_at(index, set.active);
    if (const LRESULT index = selection(id.colour); index != CB_ERR) s.*h.colour = static_cast<HoverColour>(index);
    s.*h.argb = parse_rgb(window_text(control(id.hex)), s.*h.argb);
    s.*h.fill_strength = checked(id.fill_auto) ? 0 : static_cast<std::uint8_t>(std::clamp(slider(id.fill), 2, 100));
    s.*h.line_width =
        checked(id.line_width_auto) ? 0 : static_cast<std::uint8_t>(std::clamp<std::uint16_t>(number(id.line_width), 1, 8));
    s.*h.line_opacity = checked(id.line_auto) ? 0 : static_cast<std::uint8_t>(std::clamp(slider(id.line), 10, 100));
    if (const LRESULT index = selection(id.text); index != CB_ERR) s.*h.text = static_cast<HoverText>(index);
    s.*h.text_argb = parse_rgb(window_text(control(id.text_hex)), s.*h.text_argb);
}

void ConfigureDialog::hover_values(const HoverSet& set) {
    wchar_t text[16]{};
    std::swprintf(text, 16, L"%d%%", slider(set.ids.fill));
    set_label(set.ids.fill_value, checked(set.ids.fill_auto) ? L"" : text);
    std::swprintf(text, 16, L"%d%%", slider(set.ids.line));
    set_label(set.ids.line_value, checked(set.ids.line_auto) ? L"" : text);
}

//! Each control only where the chosen style uses it.
void ConfigureDialog::hover_enabled(const HoverSet& set) {
    const Settings& s = state_.settings;
    const HoverFields& h = set.fields;
    const HoverIds& id = set.ids;
    const HoverStyle hs = s.*h.style;
    const bool hover_fill = hs == HoverStyle::fill || hs == HoverStyle::outline_fill || hs == HoverStyle::underline_fill;
    const bool hover_line = hs == HoverStyle::outline || hs == HoverStyle::outline_fill ||
                            hs == HoverStyle::underline || hs == HoverStyle::underline_fill;
    // The plain wash is always the text colour.
    const bool hover_tinted = (hs != HoverStyle::none && hs != HoverStyle::plain) || s.*h.text == HoverText::colour;
    enable(id.colour, hover_tinted);
    const bool custom_hover = hover_tinted && s.*h.colour == HoverColour::custom;
    enable(id.hex, custom_hover);
    enable(id.swatch, custom_hover);
    enable(id.text_hex, s.*h.text == HoverText::custom);
    enable(id.text_swatch, s.*h.text == HoverText::custom);
    enable(id.fill_auto, hover_fill);
    enable(id.fill, hover_fill && s.*h.fill_strength != 0);
    enable(id.fill_value, hover_fill && s.*h.fill_strength != 0);
    enable(id.line_width_auto, hover_line);
    enable(id.line_width, hover_line && s.*h.line_width != 0);
    enable(id.line_auto, hover_line);
    enable(id.line, hover_line && s.*h.line_opacity != 0);
    enable(id.line_value, hover_line && s.*h.line_opacity != 0);
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
    s.line_width = checked(IDC_LINE_WIDTH_AUTO)
                       ? 0
                       : static_cast<std::uint8_t>(std::clamp<std::uint16_t>(number(IDC_LINE_WIDTH), 1, 8));
    s.chip = checked(IDC_CHIP);
    pick(IDC_CHIP_COLOUR, s.chip_colour);
    s.chip_argb = parse_rgb(window_text(control(IDC_CHIP_HEX)), s.chip_argb);
    s.chip_strength =
        checked(IDC_CHIP_STRENGTH_AUTO) ? 0 : static_cast<std::uint8_t>(std::clamp(slider(IDC_CHIP_STRENGTH), 2, 100));
    s.corner_radius = number(IDC_RADIUS);
    s.accent_strength = checked(IDC_STRENGTH_AUTO) ? 0 : static_cast<std::uint8_t>(std::clamp(slider(IDC_STRENGTH), 2, 100));
    pick(IDC_ACCENT_SOURCE, s.accent_source);
    s.accent_argb = parse_rgb(window_text(control(IDC_ACCENT_HEX)), s.accent_argb);
    pick(IDC_BACKGROUND, s.strip_background);
    s.background_argb = parse_rgb(window_text(control(IDC_BACKGROUND_HEX)), s.background_argb);
    s.tint_strength = static_cast<std::uint8_t>(std::clamp(slider(IDC_TINT), 2, 100));
    s.transparent_background = checked(IDC_TRANSPARENT);
    s.transparent_opacity = static_cast<std::uint8_t>(std::clamp(slider(IDC_TRANSPARENT_OPACITY), 0, 100));
    s.custom_text = checked(IDC_TEXT_CUSTOM);
    s.text_argb = parse_rgb(window_text(control(IDC_TEXT_HEX)), s.text_argb);
    s.custom_active_text = checked(IDC_ACTIVE_TEXT_CUSTOM);
    s.active_text_argb = parse_rgb(window_text(control(IDC_ACTIVE_TEXT_HEX)), s.active_text_argb);

    for (const HoverSet& set : hover_sets) hover_from_controls(set);
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
    pick(IDC_TRACKS_MENU, s.tracks_menu);
    pick(IDC_DROP_NAME, s.drop_name);
    s.follow_playing = checked(IDC_FOLLOW_PLAYING);

    s.title_mode = checked(IDC_TITLE_FORMAT_MODE) ? TitleMode::format : TitleMode::playlist_name;
    // The field wraps over several lines; a pasted line break is not part of a tab title.
    std::wstring pattern = window_text(control(IDC_TITLE_PATTERN));
    std::erase_if(pattern, [](wchar_t c) { return c == L'\r' || c == L'\n'; });
    s.title_format = to_utf8(pattern);
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
    std::swprintf(text, 16, L"%d%%", slider(IDC_CHIP_STRENGTH));
    set_label(IDC_CHIP_STRENGTH_VALUE, checked(IDC_CHIP_STRENGTH_AUTO) ? L"" : text);
    std::swprintf(text, 16, L"%d%%", slider(IDC_TINT));
    set_label(IDC_TINT_VALUE, text);
    std::swprintf(text, 16, L"%d%%", slider(IDC_TRANSPARENT_OPACITY));
    set_label(IDC_TRANSPARENT_OPACITY_VALUE, text);
    for (const HoverSet& set : hover_sets) hover_values(set);
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
    const bool line = s.indicator == Indicator::underline || s.indicator == Indicator::tab_outline;
    enable(IDC_LINE_WIDTH_AUTO, line);
    enable(IDC_LINE_WIDTH, line && s.line_width != 0);
    enable(IDC_STRENGTH, fill && s.accent_strength != 0);
    enable(IDC_STRENGTH_VALUE, fill && s.accent_strength != 0);
    enable(IDC_CHIP_COLOUR, s.chip);
    const bool custom_chip = s.chip && s.chip_colour == ChipColour::custom;
    enable(IDC_CHIP_HEX, custom_chip);
    enable(IDC_CHIP_SWATCH, custom_chip);
    enable(IDC_CHIP_STRENGTH_AUTO, s.chip);
    enable(IDC_CHIP_STRENGTH, s.chip && s.chip_strength != 0);
    enable(IDC_CHIP_STRENGTH_VALUE, s.chip && s.chip_strength != 0);
    const bool custom_accent = s.accent_source == AccentSource::custom;
    enable(IDC_ACCENT_HEX, custom_accent);
    enable(IDC_ACCENT_SWATCH, custom_accent);
    const bool custom_background = s.strip_background == StripBackground::custom;
    enable(IDC_BACKGROUND_HEX, custom_background);
    enable(IDC_BACKGROUND_SWATCH, custom_background);
    const bool tint = s.strip_background == StripBackground::accent_tint;
    enable(IDC_TINT, tint);
    enable(IDC_TINT_VALUE, tint);
    enable(IDC_TRANSPARENT_OPACITY, s.transparent_background);
    enable(IDC_TRANSPARENT_OPACITY_VALUE, s.transparent_background);
    enable(IDC_TEXT_HEX, s.custom_text);
    enable(IDC_TEXT_SWATCH, s.custom_text);
    enable(IDC_ACTIVE_TEXT_HEX, s.custom_active_text);
    enable(IDC_ACTIVE_TEXT_SWATCH, s.custom_active_text);

    for (const HoverSet& set : hover_sets) hover_enabled(set);
    // The fade is shared: it matters while either set shows something.
    const bool fades = s.hover_style != HoverStyle::none || s.hover_text != HoverText::unchanged ||
                       s.active_hover_style != HoverStyle::none || s.active_hover_text != HoverText::unchanged;
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


void ConfigureDialog::finish(bool ok) {
    if (!modeless_) {
        EndDialog(ok ? IDOK : IDCANCEL);
        return;
    }
    told_ = true;
    target_.configure_closed(ok, state_);
    DestroyWindow();
}

void ConfigureDialog::on_destroy() {
    SetMsgHandled(FALSE);
    if (!modeless_) return;
    modeless_dialog_manager::g_remove(m_hWnd);
    // Destroyed with its owner (foobar2000 closing): as Cancel.
    if (!told_) {
        told_ = true;
        target_.configure_closed(false, state_);
    }
}

LRESULT ConfigureDialog::on_close_quietly(UINT, WPARAM, LPARAM) {
    told_ = true;
    DestroyWindow();
    return 0;
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
        finish(true);
        return;
    case IDCANCEL: finish(false); return;
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
    case IDC_HOVER_SWATCH:
    case IDC_HOVER_TEXT_SWATCH:
    case IDC_HOVER_ACTIVE_SWATCH:
    case IDC_HOVER_ACTIVE_TEXT_SWATCH:
    case IDC_TEXT_SWATCH:
    case IDC_ACTIVE_TEXT_SWATCH:
    case IDC_CHIP_SWATCH: {
        if (code != BN_CLICKED) return;
        const Swatch& swatch = swatch_for(id);
        const int hex = swatch.hex;
        std::uint32_t argb = state_.settings.*swatch.field;
        // Fires EN_CHANGE, and with it the usual change handling.
        if (pick_colour(m_hWnd, argb)) ::SetWindowTextW(control(hex), format_rgb(argb).c_str());
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
    if (code == BN_CLICKED && checked(id)) {
        // Ticking Automatic puts the slider (or the width) back where it rests: on the automatic
        // value.
        int bar = 0, rest = 0, width = 0;
        switch (id) {
        case IDC_STRENGTH_AUTO: bar = IDC_STRENGTH, rest = state_.dark ? auto_fill_dark : auto_fill_light; break;
        case IDC_CHIP_STRENGTH_AUTO:
            bar = IDC_CHIP_STRENGTH, rest = state_.dark ? auto_chip_dark : auto_chip_light;
            break;
        case IDC_HOVER_FILL_AUTO: bar = IDC_HOVER_FILL, rest = 10; break;
        case IDC_HOVER_ACTIVE_FILL_AUTO: bar = IDC_HOVER_ACTIVE_FILL, rest = 10; break;
        case IDC_HOVER_LINE_AUTO: bar = IDC_HOVER_LINE, rest = 50; break;
        case IDC_HOVER_ACTIVE_LINE_AUTO: bar = IDC_HOVER_ACTIVE_LINE, rest = 50; break;
        case IDC_LINE_WIDTH_AUTO: width = IDC_LINE_WIDTH, rest = auto_line_width(false); break;
        case IDC_HOVER_LINE_WIDTH_AUTO:
            width = IDC_HOVER_LINE_WIDTH, rest = auto_line_width(hover_outline(state_.settings.hover_style));
            break;
        case IDC_HOVER_ACTIVE_LINE_WIDTH_AUTO:
            width = IDC_HOVER_ACTIVE_LINE_WIDTH;
            rest = auto_line_width(hover_outline(state_.settings.active_hover_style));
            break;
        default: break;
        }
        if (bar != 0) ::SendMessageW(control(bar), TBM_SETPOS, TRUE, rest);
        if (width != 0) {
            loading_ = true;
            set_number(width, static_cast<std::uint16_t>(rest));
            loading_ = false;
        }
    }
    if (code == EN_CHANGE || code == BN_CLICKED || code == CBN_SELCHANGE) {
        settings_from_controls();
        for (const Swatch& swatch : swatches) {
            if (id == swatch.hex) ::InvalidateRect(control(swatch.button), nullptr, FALSE);
        }
        changed();
    }
}

LRESULT ConfigureDialog::on_hscroll(UINT, WPARAM, LPARAM lparam) {
    const auto bar = reinterpret_cast<HWND>(lparam);
    const auto ours = [this, bar] {
        for (const int id : {IDC_STRENGTH, IDC_CHIP_STRENGTH, IDC_TINT, IDC_TRANSPARENT_OPACITY, IDC_HOVER_FILL,
                             IDC_HOVER_LINE, IDC_HOVER_ACTIVE_FILL, IDC_HOVER_ACTIVE_LINE}) {
            if (bar == control(id)) return true;
        }
        return false;
    };
    if (loading_ || bar == nullptr || !ours()) {
        SetMsgHandled(FALSE);
        return 0;
    }
    settings_from_controls();
    changed();
    return 0;
}

void ConfigureDialog::on_draw_item(UINT, LPDRAWITEMSTRUCT item) {
    const auto is_swatch = [](UINT id) {
        for (const Swatch& swatch : swatches) {
            if (static_cast<UINT>(swatch.button) == id) return true;
        }
        return false;
    };
    if (item == nullptr || !is_swatch(item->CtlID)) {
        SetMsgHandled(FALSE);
        return;
    }
    const Swatch& swatch = swatch_for(static_cast<int>(item->CtlID));
    const std::uint32_t argb = parse_rgb(window_text(control(swatch.hex)), state_.settings.*swatch.field);
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

HWND open_configure_dialog(HWND owner, const ConfigureState& state, ConfigureTarget& target) {
    auto* dialog = new ConfigureDialog(state, target);
    if (dialog->Create(owner) == nullptr) {
        delete dialog;
        return nullptr;
    }
    const HWND wnd = dialog->m_hWnd;
    // Tab, Enter and Esc work as in a modal dialog.
    modeless_dialog_manager::g_add(wnd);
    ::ShowWindow(wnd, SW_SHOW);
    return wnd;
}

void close_configure_dialog(HWND wnd) {
    if (wnd != nullptr && ::IsWindow(wnd) != FALSE) ::SendMessageW(wnd, ConfigureDialog::wm_close_quietly, 0, 0);
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
