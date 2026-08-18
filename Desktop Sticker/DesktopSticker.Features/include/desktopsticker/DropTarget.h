#pragma once
#include <shobjidl.h>
#include <windows.h>
#include <functional>
#include <string>
#include <vector>

namespace desktopsticker {

class DropTarget : public IDropTarget {
public:
    using DropCallback = std::function<void(const std::vector<std::wstring>& paths)>;

    DropTarget(HWND hwnd, DropCallback callback);
    virtual ~DropTarget() = default;

    HWND Hwnd() const { return hwnd_; }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IDropTarget
    STDMETHODIMP DragEnter(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override;
    STDMETHODIMP DragOver(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override;
    STDMETHODIMP DragLeave() override;
    STDMETHODIMP Drop(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override;

private:
    static std::vector<std::wstring> GetPaths(IDataObject* data);

    HWND hwnd_;
    DropCallback callback_;
    ULONG refCount_ = 1;
};

} // namespace desktopsticker
