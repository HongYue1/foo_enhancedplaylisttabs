#pragma once

// The Configure dialog (per container, modal) and the small Rename dialog. They work on copies;
// the container decides what to do with the result. Keeping the container out of here means the
// dialog cannot reach into a half-updated container, and the container cannot depend on dialog
// controls.

#include <windows.h>

#include <string>

#include "../model/settings.h"

namespace ept {

struct ConfigureState {
    Settings settings;
};

class ConfigureTarget {
public:
    //! Apply `state` to the real container now (live preview). Also used to restore on Cancel.
    virtual void preview(const ConfigureState& state) noexcept = 0;

protected:
    ~ConfigureTarget() = default;
};

//! Modal. With `live`, every change is previewed on the element as it is made.
//! Returns true for OK, with `state` holding the result.
bool run_configure_dialog(HWND parent, ConfigureState& state, ConfigureTarget& target, bool live);

//! A themed Yes/No question. True for Yes.
bool run_confirm_dialog(HWND parent, const std::wstring& title, const std::wstring& text);

//! Edits a playlist's name. True for OK, with `name` holding the new name.
bool run_rename_dialog(HWND parent, std::wstring& name);

} // namespace ept
