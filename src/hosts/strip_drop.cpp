// The SDK headers first: they bring winsock2.h, which must precede any plain windows.h.
#include <helpers/foobar2000+atl.h>

#include "strip_drop.h"

#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <new>
#include <string>

#include "../platform/com_ptr.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace ept {

class StripDrop::Target final : public IDropTarget {
public:
    Target(HWND wnd, DropHandler& handler) noexcept : wnd_(wnd), handler_(&handler) {}

    void detach() noexcept {
        handler_ = nullptr;
        helper_.reset();
    }

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) noexcept override {
        if (out == nullptr) return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IDropTarget) {
            *out = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        const LONG left = InterlockedDecrement(&refs_);
        if (left == 0) delete this;
        return static_cast<ULONG>(left);
    }

    // IDropTarget
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD, POINTL pt, DWORD* effect) noexcept override {
        if (effect == nullptr) return E_INVALIDARG;
        const DWORD allowed = *effect;
        *effect = handler_ != nullptr && data != nullptr ? handler_->on_drop_enter(data, POINT{pt.x, pt.y}, allowed)
                                                         : DROPEFFECT_NONE;
        if (!helper_ && !helper_failed_) {
            helper_failed_ = FAILED(CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER,
                                                     IID_IDropTargetHelper, reinterpret_cast<void**>(helper_.put())));
        }
        if (helper_) {
            POINT p{pt.x, pt.y};
            helper_->DragEnter(wnd_, data, &p, *effect);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL pt, DWORD* effect) noexcept override {
        if (effect == nullptr) return E_INVALIDARG;
        const DWORD allowed = *effect;
        *effect = handler_ != nullptr ? handler_->on_drop_over(POINT{pt.x, pt.y}, allowed) : DROPEFFECT_NONE;
        if (helper_) {
            POINT p{pt.x, pt.y};
            helper_->DragOver(&p, *effect);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() noexcept override {
        if (handler_ != nullptr) handler_->on_drop_leave();
        if (helper_) helper_->DragLeave();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD, POINTL pt, DWORD* effect) noexcept override {
        if (effect == nullptr) return E_INVALIDARG;
        const DWORD allowed = *effect;
        if (helper_) {
            POINT p{pt.x, pt.y};
            helper_->Drop(data, &p, *effect & DROPEFFECT_COPY);
        }
        *effect = handler_ != nullptr && data != nullptr ? handler_->on_drop(data, POINT{pt.x, pt.y}, allowed)
                                                         : DROPEFFECT_NONE;
        return S_OK;
    }

private:
    ~Target() = default;

    LONG refs_{1};
    HWND wnd_;
    DropHandler* handler_;
    com_ptr<IDropTargetHelper> helper_;
    bool helper_failed_{false};
};

bool StripDrop::attach(HWND wnd, DropHandler& handler) noexcept {
    detach();
    if (wnd == nullptr) return false;
    auto* target = new (std::nothrow) Target(wnd, handler);
    if (target == nullptr) return false;
    if (FAILED(RegisterDragDrop(wnd, target))) {
        target->detach();
        target->Release();
        return false;
    }
    target_ = target; // our reference; OLE holds its own while registered
    wnd_ = wnd;
    return true;
}

void StripDrop::detach() noexcept {
    if (target_ == nullptr) return;
    target_->detach();
    if (wnd_ != nullptr && IsWindow(wnd_) != FALSE) RevokeDragDrop(wnd_);
    target_->Release();
    target_ = nullptr;
    wnd_ = nullptr;
}

namespace {

void trim_separators(std::wstring& path) {
    while (path.size() > 1 && (path.back() == L'\\' || path.back() == L'/')) path.pop_back();
}

std::wstring parent_of(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

//! The deepest folder both are in (whole components, case-insensitive).
std::wstring common_folder(const std::wstring& a, const std::wstring& b) {
    std::size_t last_sep = 0;
    bool any = false;
    const std::size_t n = (std::min)(a.size(), b.size());
    std::size_t i = 0;
    for (; i < n; ++i) {
        if (towlower(a[i]) != towlower(b[i])) break;
        if (a[i] == L'\\' || a[i] == L'/') {
            last_sep = i;
            any = true;
        }
    }
    // Both end here, or one continues with a separator: the whole shorter one is common.
    const bool a_end = i == a.size() || a[i] == L'\\' || a[i] == L'/';
    const bool b_end = i == b.size() || b[i] == L'\\' || b[i] == L'/';
    if (a_end && b_end) return a.substr(0, i);
    return any ? a.substr(0, last_sep) : std::wstring();
}

} // namespace

std::string dropped_folder_name(IDataObject* data) noexcept {
    if (data == nullptr) return {};
    try {
        FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM medium{};
        if (FAILED(data->GetData(&format, &medium))) return {};
        std::wstring common;
        bool first = true;
        const auto drop = static_cast<HDROP>(medium.hGlobal);
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i) {
            const UINT length = DragQueryFileW(drop, i, nullptr, 0);
            if (length == 0) continue;
            std::wstring path(static_cast<std::size_t>(length) + 1, L'\0');
            DragQueryFileW(drop, i, path.data(), length + 1);
            path.resize(length);
            trim_separators(path);
            // One folder names itself; files (and several folders) name what holds them.
            const DWORD attributes = GetFileAttributesW(path.c_str());
            const bool folder = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            const std::wstring dir = count == 1 && folder ? path : parent_of(path);
            common = first ? dir : common_folder(common, dir);
            first = false;
            if (common.empty()) break;
        }
        ReleaseStgMedium(&medium);
        trim_separators(common);
        const std::size_t slash = common.find_last_of(L"\\/");
        if (slash == std::wstring::npos) return {}; // "C:" or nothing: no folder to name it after
        const std::wstring name = common.substr(slash + 1);
        if (name.empty() || name.back() == L':') return {};
        const pfc::stringcvt::string_utf8_from_wide utf8(name.c_str());
        return std::string(utf8.get_ptr());
    } catch (...) {
        return {};
    }
}

} // namespace ept
