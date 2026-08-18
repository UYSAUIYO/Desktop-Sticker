#include "pch.h"
#include "desktopsticker/ShellLauncher.h"

namespace desktopsticker {

bool ShellLauncher::Open(const std::wstring& path) {
    HINSTANCE result = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

bool ShellLauncher::OpenFolderAndSelect(const std::wstring& path) {
    std::wstring params = L"/select,\"" + path + L"\"";
    HINSTANCE result = ShellExecuteW(nullptr, L"open", L"explorer.exe", params.c_str(), nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

} // namespace desktopsticker
