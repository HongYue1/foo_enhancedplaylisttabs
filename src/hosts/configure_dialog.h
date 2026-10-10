#pragma once

// The Configure dialog (per container, modal or modeless) and the small Rename dialog. They work on copies;
// the container decides what to do with the result. Keeping the container out of here means the
// dialog cannot reach into a half-updated container, and the container cannot depend on dialog
// controls.

#include <windows.h>

#include <cstdint>
#include <string>

#include "../model/settings.h"

namespace ept {

struct ConfigureState {
    Settings settings;
    //! The UI whose colours "theme" means, for the labels ("Default UI", "Columns UI").
    std::wstring ui_name{L"Default UI"};
    //! That UI's name for its highlight colour ("highlight colour", "active item frame").
    std::wstring highlight_name{L"highlight colour"};
    //! The host's font, for the Fonts page's "Default (...)" row and where its font dialog starts.
    std::wstring host_font_family;
    std::uint32_t host_font_tenths{90};
    //! The strip is dark: where the Look page's fill slider rests while Automatic is ticked.
    bool dark{true};
};

class ConfigureTarget {
public:
    //! Apply `state` to the real container now (live preview). Also used to restore on Cancel.
    virtual void preview(const ConfigureState& state) noexcept = 0;
    //! The modeless dialog closed: OK with `state` the result, or Cancel (also when it went
    //! with its owner). Not called for close_configure_dialog.
    virtual void configure_closed(bool ok, const ConfigureState& state) noexcept = 0;

protected:
    ~ConfigureTarget() = default;
};

//! Modal. With `live`, every change is previewed on the element as it is made.
//! Returns true for OK, with `state` holding the result.
bool run_configure_dialog(HWND parent, ConfigureState& state, ConfigureTarget& target, bool live);

//! Modeless and always live: the element stays usable (hover it, switch tabs) while the dialog is
//! open. Returns the dialog window, or null when it could not be created. `target` hears of the
//! end through configure_closed and must outlive the dialog (close_configure_dialog it first).
HWND open_configure_dialog(HWND owner, const ConfigureState& state, ConfigureTarget& target);

//! Closes a dialog from open_configure_dialog without telling its target.
void close_configure_dialog(HWND wnd);

//! A themed Yes/No question. True for Yes.
bool run_confirm_dialog(HWND parent, const std::wstring& title, const std::wstring& text);

//! Edits a playlist's name. True for OK, with `name` holding the new name.
bool run_rename_dialog(HWND parent, std::wstring& name);

} // namespace ept
