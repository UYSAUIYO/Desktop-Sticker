#pragma once
#include <string>

#include "desktopsticker/Export.h"

namespace desktopsticker {

class DESKTOPSTICKER_API ShellLauncher {
public:
    static bool Open(const std::wstring& path);
    static bool OpenFolderAndSelect(const std::wstring& path);
};

} // namespace desktopsticker
