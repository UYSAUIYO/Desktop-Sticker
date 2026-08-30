#pragma once
#include <string>
#include <shlobj.h>

namespace desktopsticker {

// SHGetKnownFolderPath 的 RAII 包装；失败返回空串，替代散落各处的 PWSTR + CoTaskMemFree 样板
inline std::wstring GetKnownPath(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &raw))) return {};
    std::wstring path = raw;
    CoTaskMemFree(raw);
    return path;
}

} // namespace desktopsticker
