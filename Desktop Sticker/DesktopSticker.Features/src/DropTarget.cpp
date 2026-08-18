#include "pch.h"
#include "desktopsticker/DropTarget.h"

#include <shellapi.h>
#include <shlobj.h>

namespace desktopsticker {

DropTarget::DropTarget(HWND hwnd, DropCallback callback)
    : hwnd_(hwnd), callback_(std::move(callback)) {}

STDMETHODIMP DropTarget::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
        *ppv = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) DropTarget::AddRef() { return ++refCount_; }
STDMETHODIMP_(ULONG) DropTarget::Release() {
    ULONG r = --refCount_;
    if (r == 0) delete this;
    return r;
}

STDMETHODIMP DropTarget::DragEnter(IDataObject*, DWORD, POINTL, DWORD* pdwEffect) {
    *pdwEffect = DROPEFFECT_COPY;
    return S_OK;
}

STDMETHODIMP DropTarget::DragOver(DWORD, POINTL, DWORD* pdwEffect) {
    *pdwEffect = DROPEFFECT_COPY;
    return S_OK;
}

STDMETHODIMP DropTarget::DragLeave() { return S_OK; }

STDMETHODIMP DropTarget::Drop(IDataObject* data, DWORD, POINTL, DWORD* pdwEffect) {
    *pdwEffect = DROPEFFECT_COPY;
    auto paths = GetPaths(data);
    if (!paths.empty() && callback_) callback_(paths);
    return S_OK;
}

std::vector<std::wstring> DropTarget::GetPaths(IDataObject* data) {
    std::vector<std::wstring> paths;
    FORMATETC fmt{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{};
    if (FAILED(data->GetData(&fmt, &medium))) return paths;

    HDROP drop = static_cast<HDROP>(GlobalLock(medium.hGlobal));
    if (drop) {
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i) {
            wchar_t buf[MAX_PATH]{};
            DragQueryFileW(drop, i, buf, MAX_PATH);
            paths.emplace_back(buf);
        }
        GlobalUnlock(medium.hGlobal);
    }
    ReleaseStgMedium(&medium);
    return paths;
}

} // namespace desktopsticker
