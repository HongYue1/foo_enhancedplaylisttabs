#pragma once

// OLE drop target on the strip window. A thin COM shim: it owns the
// IDropTargetHelper (drag images) and forwards everything else to a DropHandler, the
// SwitcherCore, which decides what a point on the strip means. Nothing runs unless a drag is
// over the strip: no timers or hooks while idle.

#include <windows.h>

#include <ole2.h>

#include <string>

namespace ept {

class DropHandler {
public:
    //! A drag entered (data is valid until on_drop_leave / on_drop returns). Returns the effect.
    virtual DWORD on_drop_enter(IDataObject* data, POINT screen, DWORD allowed) noexcept = 0;
    virtual DWORD on_drop_over(POINT screen, DWORD allowed) noexcept = 0;
    virtual void on_drop_leave() noexcept = 0;
    virtual DWORD on_drop(IDataObject* data, POINT screen, DWORD allowed) noexcept = 0;

protected:
    ~DropHandler() = default;
};

class StripDrop {
public:
    StripDrop() noexcept = default;
    StripDrop(const StripDrop&) = delete;
    StripDrop& operator=(const StripDrop&) = delete;
    ~StripDrop() { detach(); }

    //! RegisterDragDrop on `wnd`. False if OLE refused (the strip then takes no drops).
    bool attach(HWND wnd, DropHandler& handler) noexcept;
    //! RevokeDragDrop; the handler is never called again. Call before the window goes.
    void detach() noexcept;
    [[nodiscard]] bool attached_to(HWND wnd) const noexcept { return target_ != nullptr && wnd_ == wnd; }

private:
    class Target;
    Target* target_{nullptr};
    HWND wnd_{nullptr};
};

//! The playlist name a drop suggests: the dropped folder's name, or the name of the folder
//! that holds every dropped file or folder. Empty when there is none (no files, several
//! drives, only a drive root). UTF-8.
[[nodiscard]] std::string dropped_folder_name(IDataObject* data) noexcept;

} // namespace ept
