// The Default UI element: a playlist switcher strip around one child element, by default
// Playlist View (which shows the active playlist by itself). Everything that does not depend on
// the UI lives in SwitcherCore (switcher_core.h); this file supplies the child
// (ui_element_instance), Default UI's colours and fonts, focus routing and layout editing.
//
// Layout editing follows foo_ui_std's own containers (ui_element_helpers): in edit mode the
// child's right click reaches us and gets the standard element menu plus our items; a right click
// on the strip goes to our parent, which shows the standard menu for this element with ours.

#include <helpers/foobar2000+atl.h>

#include <helpers/ui_element_helpers.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include "../guids.h"
#include "../model/codec.h"
#include "../platform/graphics.h"
#include "../platform/logging.h"
#include "../version.h"
#include "host_util.h"
#include "switcher_core.h"

namespace ept {

namespace {

constexpr const wchar_t* window_class = L"foo_enhancedplaylisttabs_dui";
//! The id the edit tools know the child by.
constexpr unsigned child_id = 1;

enum EditCommand : unsigned {
    edit_replace,
    edit_copy,
    edit_paste,
    edit_configure,
    edit_count,
};

enum ChildCommand : unsigned {
    child_configure,
    child_count,
};

[[nodiscard]] Bytes config_bytes(const ui_element_config::ptr& cfg) {
    if (!cfg.is_valid()) return {};
    return to_bytes(cfg->get_data(), cfg->get_data_size());
}

//! Playlist View, found at run time: the SDK publishes no GUIDs for Default UI's elements.
//! Any other playlist renderer if it is missing; the empty element if there is none.
[[nodiscard]] ChildRecord default_child() {
    ChildRecord child;
    child.guid = pfc::guid_null;
    try {
        service_ptr_t<ui_element> fallback;
        service_ptr_t<ui_element> found;
        service_enum_t<ui_element> e;
        service_ptr_t<ui_element> elem;
        while (e.next(elem)) {
            if (elem->get_subclass() != ui_element_subclass_playlist_renderers) continue;
            pfc::string8 name;
            elem->get_name(name);
            if (strcmp(name.get_ptr(), "Playlist View") == 0) {
                found = elem;
                break;
            }
            if (!fallback.is_valid()) fallback = elem;
        }
        if (!found.is_valid()) found = fallback;
        if (found.is_valid()) {
            const ui_element_config::ptr cfg = found->get_default_configuration();
            child.guid = found->get_guid();
            child.config = config_bytes(cfg);
        }
    } catch (...) {
        child = ChildRecord{};
    }
    return child;
}

[[nodiscard]] InstanceData decode_or_default(const ui_element_config::ptr& cfg) {
    InstanceData data;
    try {
        if (cfg.is_valid() && cfg->get_guid() == guids::dui_element && cfg->get_data_size() != 0) {
            data = decode_instance(config_bytes(cfg));
        }
    } catch (const std::exception& e) {
        // A damaged blob must not take the whole layout down.
        log::warn(std::string("could not read settings, using defaults: ") + e.what());
        data = InstanceData{};
    }
    if (!data.has_child) {
        data.has_child = true;
        data.child = default_child();
    }
    return data;
}

[[nodiscard]] ui_element_config::ptr make_config(const InstanceData& data) {
    const Bytes bytes = encode_instance(data);
    return ui_element_config::g_create(guids::dui_element, bytes.data(), bytes.size());
}

class DuiSwitcher;

//! What the child element gets as its host. Forwards to the element until orphaned.
class ChildCallback : public ui_element_instance_callback_v3 {
public:
    explicit ChildCallback(DuiSwitcher* owner) noexcept : owner_(owner) {}
    void orphan() noexcept { owner_ = nullptr; }

    void on_min_max_info_change() override;
    void on_alt_pressed(bool) override {}
    bool query_color(const GUID& what, t_ui_color& out) override;
    bool request_activation(service_ptr_t<ui_element_instance> item) override;
    bool is_edit_mode_enabled() override;
    void request_replace(service_ptr_t<ui_element_instance> item) override;
    t_ui_font query_font_ex(const GUID& what) override;
    bool is_elem_visible(service_ptr_t<ui_element_instance> elem) override;
    t_size notify(ui_element_instance* source, const GUID& what, t_size param1, const void* param2,
                  t_size param2size) override;

private:
    DuiSwitcher* owner_;
};

class DuiSwitcher : public ui_element_instance,
                    public SwitcherCore,
                    private ui_element_helpers::ui_element_edit_tools {
public:
    explicit DuiSwitcher(ui_element_instance_callback::ptr callback) : callback_(std::move(callback)) {}
    ~DuiSwitcher() {
        // The reference count is already zero here: nothing may take a reference on us any more
        // (host_keep_alive, is_elem_visible_), or the object would be deleted twice.
        dying_ = true;
        if (wnd_ != nullptr) DestroyWindow(wnd_);
    }

    void initialize(HWND parent, const ui_element_config::ptr& cfg);

    // ui_element_instance ----------------------------------------------------------------------

    fb2k::hwnd_t get_wnd() override { return wnd_; }
    void set_configuration(ui_element_config::ptr cfg) override;
    ui_element_config::ptr get_configuration() override { return make_config(snapshot()); }
    GUID get_guid() override { return guids::dui_element; }
    GUID get_subclass() override { return ui_element_subclass_containers; }
    double get_focus_priority() override;
    void set_default_focus() override;
    bool get_focus_priority_subclass(double& out, const GUID& subclass) override;
    bool set_default_focus_subclass(const GUID& subclass) override;
    ui_element_min_max_info get_min_max_info() override;
    void notify(const GUID& what, t_size param1, const void* param2, t_size param2size) override;
    bool edit_mode_context_menu_test(const POINT&, bool) override { return true; }
    void edit_mode_context_menu_build(const POINT& point, bool from_keyboard, HMENU menu, unsigned base) override;
    void edit_mode_context_menu_command(const POINT& point, bool from_keyboard, unsigned id, unsigned base) override;

    HWND core_wnd() const noexcept override { return wnd_; }

    // From the child's callback ----------------------------------------------------------------

    void child_min_max_changed() noexcept { on_child_limits_changed(); }
    bool child_query_color(const GUID& what, t_ui_color& out) { return callback_->query_color(what, out); }
    t_ui_font child_query_font(const GUID& what) { return callback_->query_font_ex(what); }
    bool edit_mode() const noexcept {
        try {
            return callback_->is_edit_mode_enabled();
        } catch (...) {
            return false;
        }
    }
    bool child_request_activation() { return !dying_ && callback_->request_activation(this); }
    void child_request_replace();
    bool child_visible() { return host_visible(); }
    t_size child_notify(ui_element_instance* source, const GUID& what, t_size param1, const void* param2,
                        t_size param2size);

protected:
    // SwitcherCore hooks
    HWND host_child_wnd() const noexcept override { return child_wnd_; }
    void host_query_limits(Limits& out) noexcept override;
    void host_limits_changed() noexcept override {
        try {
            callback_->on_min_max_info_change();
        } catch (...) {
        }
    }
    bool host_visible() const noexcept override;
    HostColours host_colours() const noexcept override;
    void host_font(StripFont& font, StripTextOptions& options) const noexcept override;
    bool host_strip_menu(std::size_t index, POINT screen) noexcept override;
    void host_tab_key(HWND from) noexcept override;
    bool host_shortcut(WPARAM key) noexcept override;
    service_ptr_t<service_base> host_keep_alive() noexcept override {
        return dying_ ? service_ptr_t<service_base>() : service_ptr_t<service_base>(this);
    }

    // ui_element_edit_tools
    void host_replace_element(unsigned id, ui_element_config::ptr cfg) override;
    void host_replace_element(unsigned id, const GUID& guid) override;
    bool host_edit_mode_context_menu_test(unsigned, const POINT&, bool) override { return true; }
    void host_edit_mode_context_menu_build(unsigned id, const POINT& point, bool from_keyboard, HMENU menu,
                                           unsigned& id_base) override;
    void host_edit_mode_context_menu_command(unsigned id, const POINT& point, bool from_keyboard, unsigned cmd,
                                             unsigned id_base) override;

private:
    static LRESULT CALLBACK wnd_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;
    LRESULT on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;
    bool on_context_menu(WPARAM wp, LPARAM lp) noexcept;

    [[nodiscard]] InstanceData snapshot() const;
    //! The child's current configuration; the stored one while it is not created or missing.
    [[nodiscard]] ui_element_config::ptr child_config() const;
    void create_child() noexcept;
    void destroy_child() noexcept;
    void set_child(ui_element_config::ptr cfg) noexcept;

    const ui_element_instance_callback::ptr callback_;
    HWND wnd_{nullptr};
    bool dying_{false};
    ChildRecord child_;
    std::vector<RawField> unknown_sections_;
    ui_element_instance_ptr child_instance_;
    service_ptr_t<ChildCallback> child_callback_;
    HWND child_wnd_{nullptr};
};

// ---------------------------------------------------------------------------------------------
// The child callback.

void ChildCallback::on_min_max_info_change() {
    if (owner_ != nullptr) owner_->child_min_max_changed();
}

bool ChildCallback::query_color(const GUID& what, t_ui_color& out) {
    return owner_ != nullptr && owner_->child_query_color(what, out);
}

bool ChildCallback::request_activation(service_ptr_t<ui_element_instance>) {
    return owner_ != nullptr && owner_->child_request_activation();
}

bool ChildCallback::is_edit_mode_enabled() { return owner_ != nullptr && owner_->edit_mode(); }

void ChildCallback::request_replace(service_ptr_t<ui_element_instance>) {
    if (owner_ != nullptr) owner_->child_request_replace();
}

t_ui_font ChildCallback::query_font_ex(const GUID& what) {
    return owner_ != nullptr ? owner_->child_query_font(what) : nullptr;
}

bool ChildCallback::is_elem_visible(service_ptr_t<ui_element_instance>) {
    return owner_ != nullptr && owner_->child_visible();
}

t_size ChildCallback::notify(ui_element_instance* source, const GUID& what, t_size param1, const void* param2,
                             t_size param2size) {
    return owner_ != nullptr ? owner_->child_notify(source, what, param1, param2, param2size) : 0;
}

// ---------------------------------------------------------------------------------------------
// Configuration.

InstanceData DuiSwitcher::snapshot() const {
    InstanceData data;
    data.settings = settings_;
    data.unknown_settings = unknown_settings_;
    data.has_child = true;
    data.child.guid = child_.guid;
    data.child.config = child_.config;
    try {
        if (child_instance_.is_valid()) {
            const ui_element_config::ptr cfg = child_instance_->get_configuration();
            // A missing element's stand-in has no configuration of its own: keep the original.
            if (cfg.is_valid() && cfg->get_guid() == child_.guid) data.child.config = config_bytes(cfg);
        }
    } catch (...) {
    }
    data.unknown_sections = unknown_sections_;
    return data;
}

ui_element_config::ptr DuiSwitcher::child_config() const {
    const InstanceData data = snapshot();
    return ui_element_config::g_create(data.child.guid, data.child.config.data(), data.child.config.size());
}

void DuiSwitcher::set_configuration(ui_element_config::ptr cfg) {
    InstanceData data = decode_or_default(cfg);
    unknown_sections_ = std::move(data.unknown_sections);
    const bool same_child = data.child.guid == child_.guid && data.child.config == snapshot().child.config;
    reload_settings(std::move(data.settings), std::move(data.unknown_settings));
    if (same_child) return;
    if (wnd_ == nullptr) {
        child_ = std::move(data.child);
        return;
    }
    set_child(ui_element_config::g_create(data.child.guid, data.child.config.data(), data.child.config.size()));
}

// ---------------------------------------------------------------------------------------------
// Window.

void DuiSwitcher::initialize(HWND parent, const ui_element_config::ptr& cfg) {
    InstanceData data = decode_or_default(cfg);
    child_ = std::move(data.child);
    unknown_sections_ = std::move(data.unknown_sections);
    load_settings(std::move(data.settings), std::move(data.unknown_settings));
    static ATOM atom = 0;
    if (atom == 0) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = &DuiSwitcher::wnd_proc;
        wc.hInstance = core_api::get_my_instance();
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = window_class;
        atom = RegisterClassExW(&wc);
        if (atom == 0) throw std::runtime_error("could not register the element window class");
    }
    const HWND wnd = CreateWindowExW(WS_EX_CONTROLPARENT, window_class, L"",
                                     WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, 0, parent, nullptr,
                                     core_api::get_my_instance(), this);
    if (wnd == nullptr) throw std::runtime_error("could not create the element window");
    create_child();
}

LRESULT CALLBACK DuiSwitcher::wnd_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    DuiSwitcher* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = static_cast<DuiSwitcher*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self != nullptr) self->wnd_ = wnd;
    } else {
        self = reinterpret_cast<DuiSwitcher*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    }
    if (self == nullptr) return DefWindowProcW(wnd, msg, wp, lp);
    return self->on_message(wnd, msg, wp, lp);
}

LRESULT DuiSwitcher::on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    try {
        switch (msg) {
        case WM_NCDESTROY:
            SetWindowLongPtrW(wnd, GWLP_USERDATA, 0);
            wnd_ = nullptr;
            return DefWindowProcW(wnd, msg, wp, lp);
        case WM_DESTROY: {
            // The edit tools close a "Replace UI Element" dialog that is still open.
            LRESULT ignored = 0;
            ProcessWindowMessage(wnd, msg, wp, lp, ignored);
            destroy_child();
            break;
        }
        case WM_CONTEXTMENU:
            if (on_context_menu(wp, lp)) return 0;
            // Our own window or the strip: the parent shows the menu for this element.
            return DefWindowProcW(wnd, msg, wp, lp);
        default: break;
        }
        LRESULT result = 0;
        if (core_message(wnd, msg, wp, lp, result)) return result;
    } catch (const std::exception& e) {
        log::warn(std::string("element message failed: ") + e.what());
    } catch (...) {
        log::warn("element message failed");
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

bool DuiSwitcher::on_context_menu(WPARAM wp, LPARAM lp) noexcept {
    // The child forwards its right click here in layout editing mode (DefWindowProc).
    if (!edit_mode() || !child_instance_.is_valid() || child_wnd_ == nullptr) return false;
    const HWND from = reinterpret_cast<HWND>(wp);
    if (from != child_wnd_ && !IsChild(child_wnd_, from)) return false;
    const service_ptr_t<service_base> keep_alive = host_keep_alive();
    const ui_element_instance_ptr item = child_instance_;
    try {
        standard_edit_context_menu(lp, item, child_id, wnd_);
    } catch (const std::exception& e) {
        log::warn(std::string("layout menu failed: ") + e.what());
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// The child.

void DuiSwitcher::create_child() noexcept {
    if (wnd_ == nullptr || child_instance_.is_valid()) return;
    try {
        child_callback_ = new service_impl_t<ChildCallback>(this);
        const ui_element_config::ptr cfg =
            ui_element_config::g_create(child_.guid, child_.config.data(), child_.config.size());
        // Falls back to the "dummy" element (and says so in the console) if the element is missing.
        child_instance_ = ui_element_helpers::instantiate(wnd_, cfg, child_callback_);
        const HWND wnd = child_instance_.is_valid() ? child_instance_->get_wnd() : nullptr;
        if (wnd == nullptr) throw std::runtime_error("the element has no window");
        child_wnd_ = wnd;
        const RECT& rc = content_rect();
        SetWindowPos(wnd, nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        // Not SWP_SHOWWINDOW: it sends no WM_SHOWWINDOW, and Columns UI's Row/Column splitters
        // show their own children only on that message, so a hosted splitter would stay empty.
        if ((GetWindowLongPtrW(wnd, GWL_STYLE) & WS_VISIBLE) == 0) ShowWindow(wnd, SW_SHOWNA);
    } catch (const std::exception& e) {
        log::warn(std::string("could not create the element: ") + e.what());
        if (child_callback_.is_valid()) child_callback_->orphan();
        child_callback_.release();
        child_instance_.release();
        child_wnd_ = nullptr;
    }
    child_changed();
}

void DuiSwitcher::destroy_child() noexcept {
    if (child_instance_.is_valid()) {
        try {
            const ui_element_config::ptr cfg = child_instance_->get_configuration();
            if (cfg.is_valid() && cfg->get_guid() == child_.guid) child_.config = config_bytes(cfg);
        } catch (...) {
        }
        try {
            const HWND wnd = child_instance_->get_wnd();
            if (wnd != nullptr && IsWindow(wnd)) DestroyWindow(wnd);
        } catch (...) {
        }
        child_instance_.release();
    }
    if (child_callback_.is_valid()) child_callback_->orphan();
    child_callback_.release();
    child_wnd_ = nullptr;
}

void DuiSwitcher::set_child(ui_element_config::ptr cfg) noexcept {
    if (!cfg.is_valid()) return;
    try {
        ChildRecord next;
        next.guid = cfg->get_guid();
        next.config = config_bytes(cfg);
        destroy_child();
        child_ = std::move(next);
        create_child();
    } catch (const std::exception& e) {
        log::warn(std::string("could not replace the element: ") + e.what());
    }
}

void DuiSwitcher::host_query_limits(Limits& out) noexcept {
    if (!child_instance_.is_valid()) {
        SwitcherCore::host_query_limits(out);
        return;
    }
    try {
        const ui_element_min_max_info info = child_instance_->get_min_max_info();
        const auto cap = [](t_uint32 v) {
            return static_cast<unsigned>((std::min)(v, static_cast<t_uint32>(limit_cap)));
        };
        out.min_width = cap(info.m_min_width);
        out.min_height = cap(info.m_min_height);
        out.max_width = cap(info.m_max_width);
        out.max_height = cap(info.m_max_height);
    } catch (...) {
        out = Limits{};
    }
}

void DuiSwitcher::child_request_replace() {
    if (wnd_ == nullptr) return;
    const HWND anchor = child_wnd_ != nullptr ? child_wnd_ : wnd_;
    replace_dialog(anchor, child_id, child_.guid);
}

t_size DuiSwitcher::child_notify(ui_element_instance* source, const GUID& what, t_size param1, const void* param2,
                                 t_size param2size) {
    // Tabs show playlists: an element label has nowhere to go.
    if (what == ui_element_host_notify_set_elem_label) return 0;
    // Anything else (dialog texture, borders) is the host's business.
    return callback_->notify_(source, what, param1, param2, param2size);
}

// ---------------------------------------------------------------------------------------------
// Appearance and keys.

bool DuiSwitcher::host_visible() const noexcept {
    if (dying_ || wnd_ == nullptr || !IsWindowVisible(wnd_)) return false;
    try {
        return callback_->is_elem_visible_(const_cast<DuiSwitcher*>(this));
    } catch (...) {
        return true;
    }
}

HostColours DuiSwitcher::host_colours() const noexcept {
    HostColours out;
    try {
        out.background = callback_->query_std_color(ui_color_background);
        out.text = callback_->query_std_color(ui_color_text);
        out.selection = callback_->query_std_color(ui_color_selection);
        out.highlight = callback_->query_std_color(ui_color_highlight);
        out.dark = callback_->is_dark_mode();
    } catch (...) {
    }
    return out;
}

void DuiSwitcher::host_font(StripFont& font, StripTextOptions&) const noexcept {
    try {
        font.font_dpi = gfx::system_dpi();
        const auto handle = static_cast<HFONT>(callback_->query_font_ex(ui_font_tabs));
        if (handle != nullptr && GetObjectW(handle, sizeof(font.font), &font.font) == sizeof(font.font)) return;
        NONCLIENTMETRICSW metrics{sizeof(metrics)};
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
            font.font = metrics.lfMessageFont;
        }
    } catch (...) {
    }
}

bool DuiSwitcher::host_strip_menu(std::size_t, POINT screen) noexcept {
    if (!edit_mode() || wnd_ == nullptr) return false;
    const HWND parent = GetParent(wnd_);
    if (parent == nullptr) return false;
    const service_ptr_t<service_base> keep_alive = host_keep_alive();
    SendMessageW(parent, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(wnd_),
                 MAKELPARAM(static_cast<WORD>(screen.x), static_cast<WORD>(screen.y)));
    return true;
}

void DuiSwitcher::host_tab_key(HWND from) noexcept {
    const HWND root = GetAncestor(wnd_, GA_ROOT);
    if (root == nullptr) return;
    const HWND next = GetNextDlgTabItem(root, from, GetKeyState(VK_SHIFT) < 0);
    if (next != nullptr && next != from) SetFocus(next);
}

bool DuiSwitcher::host_shortcut(WPARAM key) noexcept {
    try {
        return keyboard_shortcut_manager::get()->on_keydown_auto(key);
    } catch (...) {
        return false;
    }
}

// ---------------------------------------------------------------------------------------------
// ui_element_instance.

ui_element_min_max_info DuiSwitcher::get_min_max_info() {
    ui_element_min_max_info info;
    const auto open = [](unsigned v) {
        return v >= static_cast<unsigned>(limit_cap) ? UINT32_MAX : static_cast<t_uint32>(v);
    };
    info.m_min_width = limits_.min_width;
    info.m_min_height = limits_.min_height;
    info.m_max_width = open(limits_.max_width);
    info.m_max_height = open(limits_.max_height);
    return info;
}

double DuiSwitcher::get_focus_priority() {
    return child_instance_.is_valid() ? child_instance_->get_focus_priority() : 0.0;
}

void DuiSwitcher::set_default_focus() {
    if (child_instance_.is_valid()) {
        child_instance_->set_default_focus();
    } else {
        set_default_focus_fallback();
    }
}

bool DuiSwitcher::get_focus_priority_subclass(double& out, const GUID& subclass) {
    return child_instance_.is_valid() && child_instance_->get_focus_priority_subclass(out, subclass);
}

bool DuiSwitcher::set_default_focus_subclass(const GUID& subclass) {
    return child_instance_.is_valid() && child_instance_->set_default_focus_subclass(subclass);
}

void DuiSwitcher::notify(const GUID& what, t_size param1, const void* param2, t_size param2size) {
    if (what == ui_element_notify_colors_changed) {
        refresh_colours();
    } else if (what == ui_element_notify_font_changed) {
        refresh_font();
    } else if (what == ui_element_notify_get_element_labels) {
        // The layout editing overlay: label the child (or let a container label its own).
        if (!child_instance_.is_valid() || param2 == nullptr) return;
        if (child_instance_->get_subclass() == ui_element_subclass_containers) {
            child_instance_->notify(what, param1, param2, param2size);
        } else if (child_instance_->get_guid() != pfc::guid_null && child_wnd_ != nullptr) {
            auto* labels = reinterpret_cast<ui_element_notify_get_element_labels_callback*>(const_cast<void*>(param2));
            labels->set_visible_element(child_instance_);
        }
        return;
    }
    // A copy: the child may be replaced in response.
    const ui_element_instance_ptr child = child_instance_;
    if (child.is_valid()) child->notify(what, param1, param2, param2size);
}

// ---------------------------------------------------------------------------------------------
// Layout editing.

void DuiSwitcher::edit_mode_context_menu_build(const POINT&, bool, HMENU menu, unsigned base) {
    bool can_paste = false;
    try {
        can_paste = ui_element_common_methods::get()->is_paste_available();
    } catch (...) {
    }
    AppendMenuW(menu, MF_STRING, base + edit_replace, L"Replace hosted element...");
    AppendMenuW(menu, MF_STRING | (child_.guid == pfc::guid_null ? MF_GRAYED : 0), base + edit_copy,
                L"Copy hosted element");
    AppendMenuW(menu, MF_STRING | (can_paste ? 0 : MF_GRAYED), base + edit_paste, L"Paste as hosted element");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, base + edit_configure, L"Configure " EPT_NAME L"...");
}

void DuiSwitcher::edit_mode_context_menu_command(const POINT&, bool, unsigned id, unsigned base) {
    const service_ptr_t<service_base> keep_alive = host_keep_alive();
    if (id < base || wnd_ == nullptr) return;
    switch (id - base) {
    case edit_replace: child_request_replace(); break;
    case edit_copy: ui_element_common_methods::get()->copy(child_config()); break;
    case edit_paste: host_paste_element(child_id); break;
    case edit_configure: run_configure(wnd_); break;
    default: break;
    }
}

void DuiSwitcher::host_edit_mode_context_menu_build(unsigned, const POINT&, bool, HMENU menu, unsigned& id_base) {
    AppendMenuW(menu, MF_STRING, id_base + child_configure, L"Configure " EPT_NAME L"...");
    id_base += child_count;
}

void DuiSwitcher::host_edit_mode_context_menu_command(unsigned, const POINT&, bool, unsigned cmd, unsigned id_base) {
    if (cmd < id_base) return;
    const service_ptr_t<service_base> keep_alive = host_keep_alive();
    if (cmd - id_base == child_configure) run_configure(wnd_);
}

void DuiSwitcher::host_replace_element(unsigned, const GUID& guid) {
    try {
        ui_element_config::ptr cfg;
        if (guid == pfc::guid_null) {
            cfg = ui_element_config::g_create_empty();
        } else {
            service_ptr_t<ui_element> elem;
            if (!ui_element_helpers::find(elem, guid)) return;
            // Like foo_ui_std: the new element may take over the old one's settings (a splitter
            // its children, for one).
            if (child_.guid != pfc::guid_null) cfg = elem->import(child_config());
            if (!cfg.is_valid()) cfg = elem->get_default_configuration();
        }
        host_replace_element(child_id, cfg);
    } catch (const std::exception& e) {
        log::warn(std::string("could not replace the element: ") + e.what());
    }
}

void DuiSwitcher::host_replace_element(unsigned, ui_element_config::ptr cfg) {
    if (!cfg.is_valid() || wnd_ == nullptr) return;
    set_child(cfg);
}

// ---------------------------------------------------------------------------------------------
// The element: its child for the layout tools, and taking over another element.

class Children : public ui_element_children_enumerator {
public:
    explicit Children(InstanceData data) : data_(std::move(data)) {}
    t_size get_count() override { return 1; }
    ui_element_config::ptr get_item(t_size index) override {
        if (index != 0) return ui_element_config::g_create_empty();
        return ui_element_config::g_create(data_.child.guid, data_.child.config.data(), data_.child.config.size());
    }
    bool can_set_count() override { return false; }
    void set_count(t_size) override {}
    void set_item(t_size index, ui_element_config::ptr cfg) override {
        if (index != 0 || !cfg.is_valid()) return;
        data_.child.guid = cfg->get_guid();
        data_.child.config = config_bytes(cfg);
    }
    ui_element_config::ptr commit() override { return make_config(data_); }

private:
    InstanceData data_;
};

class DuiElement : public ui_element_v2 {
public:
    GUID get_guid() override { return guids::dui_element; }
    GUID get_subclass() override { return ui_element_subclass_containers; }
    void get_name(pfc::string_base& out) override { out = EPT_NAME; }
    bool get_description(pfc::string_base& out) override {
        out = "Fast playlist switching tabs around Playlist View (or another element).";
        return true;
    }
    t_uint32 get_flags() override { return 0; }
    bool bump() override { return false; }

    ui_element_instance_ptr instantiate(HWND parent, ui_element_config::ptr cfg,
                                        ui_element_instance_callback_ptr callback) override {
        PFC_ASSERT(cfg->get_guid() == get_guid());
        auto instance = fb2k::service_new<DuiSwitcher>(callback);
        instance->initialize(parent, cfg);
        return instance;
    }

    ui_element_config::ptr get_default_configuration() override { return make_config(decode_or_default(nullptr)); }

    ui_element_children_enumerator_ptr enumerate_children(ui_element_config::ptr cfg) override {
        return new service_impl_t<Children>(decode_or_default(cfg));
    }

    ui_element_config::ptr import(ui_element_config::ptr cfg) override {
        if (!cfg.is_valid()) return nullptr;
        if (cfg->get_guid() == get_guid()) return cfg;
        if (cfg->get_guid() == pfc::guid_null) return nullptr;
        // A container with exactly one child (Default UI's Playlist Tabs): take over that child.
        // Anything else is replaced, not wrapped: null gives the default (Playlist View).
        try {
            const ui_element_children_enumerator_ptr children = ui_element_helpers::enumerate_children(cfg);
            if (!children.is_valid() || children->get_count() != 1) return nullptr;
            const ui_element_config::ptr item = children->get_item(0);
            if (!item.is_valid() || item->get_guid() == pfc::guid_null) return nullptr;
            InstanceData data;
            data.has_child = true;
            data.child.guid = item->get_guid();
            data.child.config = config_bytes(item);
            return make_config(data);
        } catch (...) {
            return nullptr;
        }
    }
};

FB2K_SERVICE_FACTORY(DuiElement);

} // namespace

} // namespace ept
