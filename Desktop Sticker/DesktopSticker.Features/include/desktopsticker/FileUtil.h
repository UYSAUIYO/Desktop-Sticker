#pragma once
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace desktopsticker {

// 原子写盘：先写同目录 .tmp，再 MoveFileEx 原子替换。
// 配置/布局中途损坏的容忍成本远高于多一次替换，所有 JSON 落盘必须走这里。
inline bool WriteFileAtomic(const std::filesystem::path& target, std::string_view content) {
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    const std::filesystem::path tmp = target.wstring() + L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out.good()) return false;
    }
    return MoveFileExW(tmp.c_str(), target.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

} // namespace desktopsticker
