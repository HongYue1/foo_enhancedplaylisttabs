// The Columns UI container: a uie::splitter_window_v3 with one panel (NG Playlist by default)
// under the playlist tab strip. Everything that does not depend on the UI lives in SwitcherCore
// (switcher_core.h); this file supplies the panel (uie::window), the host handed to it, Columns
// UI's colours and fonts, and the splitter interface the Layout page and live editing use.
//
// Columns UI is optional: nothing here runs unless it is installed, and the DLL imports nothing
// from it (the SDK is a static library of interfaces and helpers).

#include <helpers/foobar2000+atl.h>

#include <columns_ui-sdk/ui_extension.h>

#include <string>
#include <utility>
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

[[nodiscard]] Bytes array_bytes(const pfc::array_t<t_uint8>& data) {
    return Bytes(data.get_ptr(), data.get_ptr() + data.get_size());
}

[[nodiscard]] Bytes read_all(stream_reader* reader, t_size size, abort_callback& abort) {
    Bytes bytes(size);
    if (size != 0) reader->read_object(bytes.data(), size, abort);
    return bytes;
}

//! NG Playlist, with no settings of its own yet (it takes its defaults when created).
//! Never creates a panel object: uie::window::create_by_guid() instantiates every panel to compare
//! GUIDs, this container included, so doing it from our constructor recursed until the stack
//! overflowed (at start-up, depending on the order the factories enumerate in).
void default_child(bool& has_child, ChildRecord& child) noexcept {
    has_child = true;
    child = ChildRecord{};
    child.guid = cui::panels::guid_playlist_view_v2;
}

class CuiHost;

class CuiSwitcher : public uie::container_uie_window_v3_t<uie::splitter_window_v3>, public SwitcherCore {
public:
    CuiSwitcher() {
        default_child(has_child_, child_);
        load_settings(Settings{}, {});
    }

    // uie::extension_base / uie::window ------------------------------------------------------

    const GUID& get_extension_guid() const override { return guids::cui_container; }
    void get_name(pfc::string_base& out) const override { out = EPT_NAME; }
    void get_category(pfc::string_base& out) const override { out = "Splitters"; }
    bool get_description(pfc::string_base& out) const override {
        out = "Fast playlist switching tabs around NG Playlist (or another panel).";
        return true;
    }
    unsigned get_type() const override { return uie::type_layout | uie::type_splitter; }

    void set_config(stream_reader* reader, t_size size, abort_callback& abort) override;
    void get_config(stream_writer* writer, abort_callback& abort) const override;
    void import_config(stream_reader* reader, t_size size, abort_callback& abort) override;
    void export_config(stream_writer* writer, abort_callback& abort) const override;
    bool have_config_popup() const override { return true; }
    //! Also called by the Layout page on an instance without a window (it reads get_config after):
    //! modal, so the page gets the result.
    bool show_config_popup(HWND parent) override { return run_configure(parent, false); }

    uie::container_window_v3_config get_window_config() override {
        // Not transparent: that would repaint the whole container on every move and resize.
        return {L"foo_enhancedplaylisttabs_cui", false};
    }
    LRESULT on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) override;

    // uie::splitter_window ----------------------------------------------------------------

    void insert_panel(t_size index, const uie::splitter_item_t* item) override;
    void remove_panel(t_size index) override;
    void replace_panel(t_size index, const uie::splitter_item_t* item) override;
    t_size get_panel_count() const override { return has_child_ ? 1 : 0; }
    t_size get_maximum_panel_count() const override { return 1; }
    void get_supported_panels(const pfc::list_base_const_t<uie::window::ptr>& windows,
                              bit_array_var& mask_unsupported) override;
    void reorder_panels(const size_t*, size_t) override {}
    //! Live editing (Ctrl+Shift+right click) walks the layout through this.
    bool is_point_ours(HWND wnd_point, const POINT& pt_screen, pfc::list_base_t<uie::window_ptr>& hierarchy) override;

    // For the host -------------------------------------------------------------------------

    [[nodiscard]] bool owns(HWND wnd) const noexcept { return wnd != nullptr && wnd == child_wnd_; }
    void child_limits_changed() noexcept { on_child_limits_changed(); }
    void relinquish(HWND wnd) noexcept;
    void get_children(pfc::list_base_t<uie::window_ptr>& out) const;
    [[nodiscard]] bool self_visible() const;

    HWND core_wnd() const noexcept override { return get_wnd(); }

protected:
    uie::splitter_item_t* get_panel(t_size index) const override;

    // SwitcherCore hooks
    HWND host_child_wnd() const noexcept override { return child_wnd_; }
    void host_limits_changed() noexcept override;
    bool host_visible() const noexcept override;
    HostColours host_colours() const noexcept override;
    void host_font(StripFont& font, StripTextOptions& options) const noexcept override;
    void host_tab_key(HWND from) noexcept override;
    bool host_shortcut(WPARAM key) noexcept override;
    service_ptr_t<service_base> host_keep_alive() noexcept override { return this; }
    const wchar_t* host_ui_name() const noexcept override { return L"Columns UI"; }
    const wchar_t* host_highlight_name() const noexcept override { return L"active item frame"; }

private:
    [[nodiscard]] InstanceData snapshot() const;
    //! Settings and panel from a configuration; `object` is a panel already configured for it.
    void apply(InstanceData data, uie::window_ptr object);
    [[nodiscard]] uie::window_host_ptr host_for_availability() const;
    void create_child() noexcept;
    void destroy_child() noexcept;
    void set_child(bool has_child, ChildRecord child, uie::window_ptr object) noexcept;
    void set_child_from_item(const uie::splitter_item_t* item) noexcept;

    bool has_child_{false};
    ChildRecord child_;
    std::vector<RawField> unknown_sections_;
    //! The panel object. Kept between windows only when import_config configured it.
    uie::window_ptr child_window_;
    HWND child_wnd_{nullptr};
    service_ptr_t<CuiHost> host_;
};

// ---------------------------------------------------------------------------------------------
// The host handed to the panel. Null-safe: once the container's window is gone it answers "no"
// to everything, and the default-constructed instance registered below does the same.

class CuiHost : public uie::window_host_ex {
public:
    CuiHost() = default;
    explicit CuiHost(CuiSwitcher* owner) noexcept : owner_(owner) {}
    void detach() noexcept { owner_ = nullptr; }

    const GUID& get_host_guid() const override { return guids::cui_host; }

    bool get_keyboard_shortcuts_enabled() const override {
        if (owner_ == nullptr) return true;
        const auto& parent = owner_->get_host();
        return !parent.is_valid() || parent->get_keyboard_shortcuts_enabled();
    }

    void on_size_limit_change(HWND wnd, unsigned) override {
        if (owner_ != nullptr && owner_->owns(wnd)) owner_->child_limits_changed();
    }

    unsigned is_resize_supported(HWND) const override { return 0; }
    bool request_resize(HWND, unsigned, unsigned, unsigned) override { return false; }

    bool override_status_text_create(service_ptr_t<ui_status_text_override>& out) override {
        if (owner_ == nullptr) return false;
        const auto& parent = owner_->get_host();
        return parent.is_valid() && parent->override_status_text_create(out);
    }

    bool is_visible(HWND wnd) const override {
        return owner_ != nullptr && owner_->self_visible() && IsWindowVisible(wnd) != FALSE;
    }

    bool is_visibility_modifiable(HWND wnd, bool desired_visibility) const override {
        if (owner_ == nullptr || !desired_visibility || !owner_->owns(wnd)) return false;
        if (owner_->self_visible()) return true;
        const auto& parent = owner_->get_host();
        return parent.is_valid() && parent->is_visibility_modifiable(owner_->get_wnd(), true);
    }

    bool set_window_visibility(HWND wnd, bool visibility) override {
        if (owner_ == nullptr || !visibility || !owner_->owns(wnd)) return false;
        if (owner_->self_visible()) return true;
        const auto& parent = owner_->get_host();
        return parent.is_valid() && parent->set_window_visibility(owner_->get_wnd(), true);
    }

    void relinquish_ownership(HWND wnd) override {
        if (owner_ != nullptr) owner_->relinquish(wnd);
    }

    void get_children(pfc::list_base_t<uie::window_ptr>& out) override {
        if (owner_ != nullptr) owner_->get_children(out);
    }

private:
    CuiSwitcher* owner_{nullptr};
};

uie::window_host_factory<CuiHost> g_host_factory;

// ---------------------------------------------------------------------------------------------
// Configuration.

InstanceData CuiSwitcher::snapshot() const {
    InstanceData data;
    data.settings = settings_;
    data.unknown_settings = unknown_settings_;
    data.has_child = has_child_;
    data.child = child_;
    if (has_child_ && child_wnd_ != nullptr && child_window_.is_valid()) {
        try {
            pfc::array_t<t_uint8> live;
            child_window_->get_config_to_array(live, fb2k::noAbort, true);
            data.child.config = array_bytes(live);
        } catch (...) {
        }
    }
    data.unknown_sections = unknown_sections_;
    return data;
}

void CuiSwitcher::apply(InstanceData data, uie::window_ptr object) {
    unknown_sections_ = std::move(data.unknown_sections);
    reload_settings(std::move(data.settings), std::move(data.unknown_settings));
    const InstanceData now = snapshot();
    if (!object.is_valid() && data.has_child == now.has_child &&
        (!data.has_child || (data.child.guid == now.child.guid && data.child.config == now.child.config))) {
        return;
    }
    set_child(data.has_child, std::move(data.child), std::move(object));
}

void CuiSwitcher::set_config(stream_reader* reader, t_size size, abort_callback& abort) {
    InstanceData data;
    if (size == 0) {
        // A panel just added on the Layout page or by another container: the defaults.
        default_child(data.has_child, data.child);
        apply(std::move(data), nullptr);
        return;
    }
    try {
        data = decode_instance(read_all(reader, size, abort));
    } catch (const std::exception& e) {
        // A damaged blob must not take the whole layout down.
        log::warn(std::string("could not read settings, using defaults: ") + e.what());
        data = InstanceData{};
        default_child(data.has_child, data.child);
    }
    apply(std::move(data), nullptr);
}

void CuiSwitcher::get_config(stream_writer* writer, abort_callback& abort) const {
    const Bytes bytes = encode_instance(snapshot());
    writer->write(bytes.data(), bytes.size(), abort);
}

void CuiSwitcher::export_config(stream_writer* writer, abort_callback& abort) const {
    InstanceData data = snapshot();
    if (data.has_child) {
        uie::window_ptr child = child_window_;
        if (!child.is_valid()) {
            // Like Columns UI's own splitters: an FCL that silently dropped the panel would be worse.
            if (!uie::window::create_by_guid(data.child.guid, child)) throw cui::fcl::exception_missing_panel();
            try {
                if (!data.child.config.empty()) {
                    child->set_config_from_ptr(data.child.config.data(), data.child.config.size(), abort);
                }
            } catch (const exception_io&) {
            }
        }
        pfc::array_t<t_uint8> exported;
        child->export_config_to_array(exported, abort, true);
        data.child.config = array_bytes(exported);
    }
    const Bytes bytes = encode_instance(data);
    writer->write(bytes.data(), bytes.size(), abort);
}

void CuiSwitcher::import_config(stream_reader* reader, t_size size, abort_callback& abort) {
    InstanceData data = decode_instance(read_all(reader, size, abort));
    uie::window_ptr object;
    if (data.has_child) {
        if (uie::window::create_by_guid(data.child.guid, object)) {
            try {
                if (!data.child.config.empty()) {
                    object->import_config_from_ptr(data.child.config.data(), data.child.config.size(), abort);
                }
            } catch (const exception_io&) {
            }
            pfc::array_t<t_uint8> config;
            object->get_config_to_array(config, abort, true);
            data.child.config = array_bytes(config);
        } else {
            data.child.config.clear();
        }
    }
    apply(std::move(data), std::move(object));
}

// ---------------------------------------------------------------------------------------------
// Window.

LRESULT CuiSwitcher::on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    LRESULT result = 0;
    switch (msg) {
    case WM_CREATE:
        try {
            host_ = fb2k::service_new<CuiHost>(this);
        } catch (...) {
        }
        core_message(wnd, msg, wp, lp, result);
        create_child();
        return 0;
    case WM_DESTROY:
        destroy_child();
        core_message(wnd, msg, wp, lp, result);
        if (host_.is_valid()) host_->detach();
        host_.release();
        return 0;
    default: break;
    }
    if (core_message(wnd, msg, wp, lp, result)) return result;
    return DefWindowProc(wnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------------------------
// The panel.

uie::window_host_ptr CuiSwitcher::host_for_availability() const {
    if (host_.is_valid()) return host_;
    // Ownerless: a panel that kept this host from is_available() could never reach a dead container.
    return fb2k::service_new<CuiHost>();
}

void CuiSwitcher::create_child() noexcept {
    const HWND self = get_wnd();
    if (self == nullptr || !has_child_ || child_wnd_ != nullptr) return;
    try {
        uie::window_ptr object = child_window_;
        if (!object.is_valid()) {
            if (!uie::window::create_by_guid(child_.guid, object)) {
                log::warn("the hosted panel is not installed");
                child_changed();
                return;
            }
            try {
                // No settings yet (a new container): the panel keeps its defaults.
                if (!child_.config.empty()) {
                    object->set_config_from_ptr(child_.config.data(), child_.config.size(), fb2k::noAbort);
                }
            } catch (const exception_io& e) {
                log::warn(std::string("the hosted panel rejected its settings: ") + e.what());
            }
        }
        if (!object->is_available(host_for_availability())) {
            log::warn("the hosted panel is not available here");
            child_window_ = object;
            child_changed();
            return;
        }
        const RECT& rc = content_rect();
        const HWND wnd = object->create_or_transfer_window(self, host_, ui_helpers::window_position_t(rc));
        if (wnd == nullptr) throw std::runtime_error("the panel has no window");
        child_window_ = object;
        child_wnd_ = wnd;
        SetWindowPos(wnd, nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        // Not SWP_SHOWWINDOW: it sends no WM_SHOWWINDOW, and Columns UI's Row/Column splitters
        // show their own children only on that message, so a hosted splitter would stay empty.
        if ((GetWindowLongPtrW(wnd, GWL_STYLE) & WS_VISIBLE) == 0) ShowWindow(wnd, SW_SHOWNA);
    } catch (const std::exception& e) {
        log::warn(std::string("could not create the hosted panel: ") + e.what());
        child_wnd_ = nullptr;
    }
    child_changed();
}

void CuiSwitcher::destroy_child() noexcept {
    if (child_wnd_ != nullptr && child_window_.is_valid()) {
        try {
            pfc::array_t<t_uint8> live;
            child_window_->get_config_to_array(live, fb2k::noAbort, true);
            child_.config = array_bytes(live);
        } catch (...) {
        }
        try {
            child_window_->destroy_window();
        } catch (...) {
        }
    }
    child_window_.release();
    child_wnd_ = nullptr;
}

void CuiSwitcher::set_child(bool has_child, ChildRecord child, uie::window_ptr object) noexcept {
    try {
        destroy_child();
        has_child_ = has_child;
        child_ = has_child ? std::move(child) : ChildRecord{};
        if (has_child) child_window_ = std::move(object);
        if (get_wnd() != nullptr) {
            create_child();
            limits_changed();
        }
    } catch (const std::exception& e) {
        log::warn(std::string("could not replace the hosted panel: ") + e.what());
    }
}

void CuiSwitcher::set_child_from_item(const uie::splitter_item_t* item) noexcept {
    try {
        ChildRecord child;
        child.guid = item->get_panel_guid();
        pfc::array_t<t_uint8> config;
        item->get_panel_config_to_array(config, true);
        child.config = array_bytes(config);
        set_child(true, std::move(child), nullptr);
    } catch (const std::exception& e) {
        log::warn(std::string("could not take the panel: ") + e.what());
    }
}

void CuiSwitcher::relinquish(HWND wnd) noexcept {
    // The panel moved to another host: forget it without destroying it.
    if (!owns(wnd)) return;
    child_window_.release();
    child_wnd_ = nullptr;
    has_child_ = false;
    child_ = ChildRecord{};
    child_changed();
    limits_changed();
}

void CuiSwitcher::get_children(pfc::list_base_t<uie::window_ptr>& out) const {
    if (child_wnd_ != nullptr && child_window_.is_valid()) out.add_item(child_window_);
}

bool CuiSwitcher::self_visible() const {
    const HWND self = get_wnd();
    if (self == nullptr) return false;
    const auto& parent = get_host();
    return parent.is_valid() ? parent->is_visible(self) : IsWindowVisible(self) != FALSE;
}

// ---------------------------------------------------------------------------------------------
// splitter_window.

void CuiSwitcher::insert_panel(t_size index, const uie::splitter_item_t* item) {
    if (item == nullptr || index != 0 || has_child_) return;
    set_child_from_item(item);
}

void CuiSwitcher::replace_panel(t_size index, const uie::splitter_item_t* item) {
    if (item == nullptr || index != 0) return;
    set_child_from_item(item);
}

void CuiSwitcher::remove_panel(t_size index) {
    if (index != 0 || !has_child_) return;
    set_child(false, ChildRecord{}, nullptr);
}

uie::splitter_item_t* CuiSwitcher::get_panel(t_size index) const {
    if (index != 0 || !has_child_) return nullptr;
    const InstanceData data = snapshot();
    auto* item = new uie::splitter_item_full_v3_impl_t;
    item->set_panel_guid(data.child.guid);
    item->set_panel_config_from_ptr(data.child.config.data(), data.child.config.size());
    if (child_wnd_ != nullptr) item->set_window_ptr(child_window_);
    item->m_custom_title = false;
    item->m_hidden = false;
    item->m_autohide = false;
    item->m_caption_orientation = 0;
    item->m_locked = false;
    item->m_show_toggle_area = false;
    item->m_show_caption = false;
    item->m_size = 150;
    item->m_size_v2 = 150;
    item->m_size_v2_dpi = USER_DEFAULT_SCREEN_DPI;
    return item;
}

void CuiSwitcher::get_supported_panels(const pfc::list_base_const_t<uie::window::ptr>& windows,
                                       bit_array_var& mask_unsupported) {
    const uie::window_host_ptr host = host_for_availability();
    const t_size count = windows.get_count();
    for (t_size i = 0; i < count; ++i) mask_unsupported.set(i, !windows[i]->is_available(host));
}

bool CuiSwitcher::is_point_ours(HWND wnd_point, const POINT& pt_screen, pfc::list_base_t<uie::window_ptr>& hierarchy) {
    // Same shape as Columns UI's own splitters: the container and its strip select the container;
    // a point in the panel selects the panel with the container as its parent, recursing into a
    // panel that is itself a splitter.
    const HWND self = get_wnd();
    if (self == nullptr || wnd_point == nullptr) return false;
    if (wnd_point != self && !IsChild(self, wnd_point)) return false;
    if (child_wnd_ != nullptr && child_window_.is_valid() &&
        (wnd_point == child_wnd_ || IsChild(child_wnd_, wnd_point))) {
        uie::splitter_window_v2_ptr splitter;
        if (child_window_->service_query_t(splitter)) {
            pfc::list_t<uie::window_ptr> nested;
            nested.add_item(this);
            if (splitter->is_point_ours(wnd_point, pt_screen, nested)) {
                hierarchy.add_items(nested);
                return true;
            }
        }
        hierarchy.add_item(this);
        hierarchy.add_item(child_window_);
        return true;
    }
    hierarchy.add_item(this);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Appearance and keys.

void CuiSwitcher::host_limits_changed() noexcept {
    const HWND self = get_wnd();
    const auto& parent = get_host();
    if (self == nullptr || !parent.is_valid()) return;
    try {
        parent->on_size_limit_change(self, uie::size_limit_all);
    } catch (...) {
    }
}

bool CuiSwitcher::host_visible() const noexcept {
    try {
        return self_visible();
    } catch (...) {
        return false;
    }
}

HostColours CuiSwitcher::host_colours() const noexcept {
    HostColours out;
    try {
        const cui::colours::helper colours(guids::cui_colour_client);
        out.background = colours.get_colour(cui::colours::colour_background);
        out.text = colours.get_colour(cui::colours::colour_text);
        out.selection = colours.get_colour(cui::colours::colour_selection_background);
        out.highlight = colours.get_colour(cui::colours::colour_active_item_frame);
        out.dark = colours.is_dark_mode_active();
        // What Columns UI paints behind its splitters (their dividers show through).
        out.layout = out.dark ? RGB(51, 51, 51) : GetSysColor(COLOR_BTNFACE);
    } catch (...) {
    }
    return out;
}

void CuiSwitcher::host_font(StripFont& font, StripTextOptions& options) const noexcept {
    try {
        font.font = cui::fonts::get_log_font_with_fallback(guids::cui_font_client);
        font.font_dpi = gfx::system_dpi();
        cui::fonts::rendering_options::ptr rendering;
        try {
            // Columns UI 3+: the DirectWrite font, its emoji fallback and the text rendering
            // options. Earlier versions have no manager_v3 and leave `font.family` empty.
            if (const cui::fonts::font::ptr dw = cui::fonts::get_font(guids::cui_font_client); dw.is_valid()) {
                if (const wchar_t* family = dw->family_name(); family != nullptr) font.family = family;
                font.weight = dw->weight();
                font.style = dw->style();
                font.stretch = dw->stretch();
                font.size_dip = dw->size();
                IDWriteFontFallback* fallback = nullptr;
                if (SUCCEEDED(dw->create_font_fallback(&fallback)) && fallback != nullptr) {
                    *font.fallback.put() = fallback;
                }
                rendering = dw->rendering_options();
            }
        } catch (...) {
            font.family.clear();
        }
        if (rendering.is_valid()) {
            options.antialias = rendering->rendering_mode() == DWRITE_RENDERING_MODE_ALIASED ? TextAntialias::aliased
                                : rendering->use_greyscale_antialiasing()                    ? TextAntialias::greyscale
                                                                                             : TextAntialias::automatic;
            options.gdi_compatible = rendering->use_gdi_compatible_layout();
            options.gdi_natural = rendering->use_gdi_natural();
            options.colour_glyphs = rendering->use_colour_glyphs();
            const HWND self = get_wnd();
            if (IDWriteFactory* factory = gfx::dwrite(); factory != nullptr && self != nullptr) {
                IDWriteRenderingParams* params = nullptr;
                if (SUCCEEDED(rendering->create_rendering_params(
                        factory, MonitorFromWindow(self, MONITOR_DEFAULTTONEAREST), &params)) &&
                    params != nullptr) {
                    *options.params.put() = params;
                }
            }
        }
    } catch (...) {
    }
}

void CuiSwitcher::host_tab_key(HWND from) noexcept {
    try {
        uie::window::g_on_tab(from);
    } catch (...) {
    }
}

bool CuiSwitcher::host_shortcut(WPARAM key) noexcept {
    try {
        const auto& parent = get_host();
        if (parent.is_valid() && !parent->get_keyboard_shortcuts_enabled()) return false;
        return uie::window::g_process_keydown_keyboard_shortcuts(key);
    } catch (...) {
        return false;
    }
}

uie::window_factory<CuiSwitcher> g_container_factory;

// ---------------------------------------------------------------------------------------------
// Colours and fonts pages. Singletons: fan changes out to the live containers (the Default UI
// ones too: they ignore Columns UI's settings, and refreshing them is harmless).

void refresh_all_colours() noexcept {
    for (SwitcherCore* core : SwitcherCore::live()) core->refresh_colours();
}

void refresh_all_fonts() noexcept {
    for (SwitcherCore* core : SwitcherCore::live()) core->refresh_font();
}

class ColourClient : public cui::colours::client {
public:
    const GUID& get_client_guid() const override { return guids::cui_colour_client; }
    void get_name(pfc::string_base& out) const override { out = EPT_NAME; }
    uint32_t get_supported_colours() const override {
        return cui::colours::colour_flag_background | cui::colours::colour_flag_text |
               cui::colours::colour_flag_selection_background | cui::colours::colour_flag_active_item_frame;
    }
    uint32_t get_supported_bools() const override { return cui::colours::bool_flag_dark_mode_enabled; }
    bool get_themes_supported() const override { return false; }
    void on_colour_changed(uint32_t) const override { refresh_all_colours(); }
    void on_bool_changed(uint32_t mask) const override {
        if ((mask & cui::colours::bool_flag_dark_mode_enabled) != 0) refresh_all_colours();
    }
};

cui::colours::client::factory<ColourClient> g_colour_client;

class FontClient : public cui::fonts::client {
public:
    const GUID& get_client_guid() const override { return guids::cui_font_client; }
    void get_name(pfc::string_base& out) const override { out = EPT_NAME ": tabs"; }
    cui::fonts::font_type_t get_default_font_type() const override { return cui::fonts::font_type_labels; }
    void on_font_changed() const override { refresh_all_fonts(); }
};

cui::fonts::client::factory<FontClient> g_font_client;

} // namespace

} // namespace ept
