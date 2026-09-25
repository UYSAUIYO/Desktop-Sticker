#pragma once

#include <cstdint>
#include <cwctype>
#include <string>

namespace desktopsticker::resmon {

enum class StorageCategory {
    Wallpaper, Ffmpeg, Assets, Pdb, Program, Runtime, Config, Logs, Other, Count
};

struct ClassifyRoots {
    std::wstring exeDir;
    std::wstring configDir;
    std::wstring wallpaperRoot; // 可空
};

namespace detail {

inline std::wstring lower(std::wstring s) {
    for (wchar_t& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

inline std::wstring trim_sep(std::wstring s) {
    while (!s.empty() && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
    return s;
}

inline std::wstring parent_of(const std::wstring& p) {
    const size_t pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring{} : p.substr(0, pos);
}

inline std::wstring name_of(const std::wstring& p) {
    const size_t pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? p : p.substr(pos + 1);
}

inline std::wstring ext_of(const std::wstring& p) {
    const std::wstring n = name_of(p);
    const size_t pos = n.find_last_of(L'.');
    return pos == std::wstring::npos ? std::wstring{} : n.substr(pos);
}

// 路径分量感知：C:\a\ffmpeg-sdk 不属于 C:\a\ffmpeg
inline bool is_under(const std::wstring& path, const std::wstring& root) {
    if (root.empty()) return false;
    const std::wstring p = lower(trim_sep(path));
    const std::wstring r = lower(trim_sep(root));
    if (p.size() <= r.size()) return false;
    if (p.compare(0, r.size(), r) != 0) return false;
    return p[r.size()] == L'\\' || p[r.size()] == L'/';
}

inline bool is_direct_child(const std::wstring& path, const std::wstring& root) {
    if (root.empty()) return false;
    return lower(trim_sep(parent_of(path))) == lower(trim_sep(root));
}

} // namespace detail

// 规格 6.3 的 9 条规则，按顺序首个命中即返回
inline StorageCategory classify_path(const std::wstring& path, const ClassifyRoots& roots) {
    using namespace detail;

    // 先规整 exeDir：来源可能自带尾分隔符，直接拼接会得到 "C:\App\\ffmpeg" 而漏匹配
    const std::wstring exe = trim_sep(roots.exeDir);

    if (is_under(path, roots.wallpaperRoot)) return StorageCategory::Wallpaper;
    if (is_under(path, exe + L"\\ffmpeg")) return StorageCategory::Ffmpeg;
    if (is_under(path, exe + L"\\assets")) return StorageCategory::Assets;

    if (is_direct_child(path, exe) && lower(ext_of(path)) == L".pdb") {
        return StorageCategory::Pdb;
    }

    if (is_direct_child(path, exe)) {
        const std::wstring n = lower(name_of(path));
        if (n == L"desktop_sticker.exe" || n == L"desktopsticker.features.dll" ||
            n == L"desktopsticker.wallpaper.dll" || n == L"desktopsticker.resmon.dll") {
            return StorageCategory::Program;
        }
    }

    if (is_under(path, exe)) return StorageCategory::Runtime;

    if (is_direct_child(path, roots.configDir)) {
        const std::wstring n = lower(name_of(path));
        if (n == L"config.json" || n == L"apps.json" ||
            n == L"layout.json" || n == L"wallpaper.json") {
            return StorageCategory::Config;
        }
        if (n == L"debug.log" || lower(ext_of(path)) == L".bak") {
            return StorageCategory::Logs;
        }
    }

    return StorageCategory::Other;
}

inline const wchar_t* category_id(StorageCategory c) {
    switch (c) {
        case StorageCategory::Wallpaper: return L"wallpaper";
        case StorageCategory::Ffmpeg:    return L"ffmpeg";
        case StorageCategory::Assets:    return L"assets";
        case StorageCategory::Pdb:       return L"pdb";
        case StorageCategory::Program:   return L"program";
        case StorageCategory::Runtime:   return L"runtime";
        case StorageCategory::Config:    return L"config";
        case StorageCategory::Logs:      return L"logs";
        default:                         return L"other";
    }
}

inline const wchar_t* category_name(StorageCategory c) {
    switch (c) {
        case StorageCategory::Wallpaper: return L"壁纸媒体库";
        case StorageCategory::Ffmpeg:    return L"FFmpeg 负载";
        case StorageCategory::Assets:    return L"天气图标资源";
        case StorageCategory::Pdb:       return L"调试符号";
        case StorageCategory::Program:   return L"程序主体";
        case StorageCategory::Runtime:   return L"运行时与框架";
        case StorageCategory::Config:    return L"配置与布局";
        case StorageCategory::Logs:      return L"日志";
        default:                         return L"其他";
    }
}

inline const wchar_t* category_note(StorageCategory c) {
    switch (c) {
        case StorageCategory::Pdb:     return L"可安全删除（仅影响调试）";
        case StorageCategory::Runtime: return L"不建议删除";
        default:                       return nullptr;
    }
}

inline uint32_t category_color(StorageCategory c) {
    switch (c) {
        case StorageCategory::Wallpaper: return 0x2E7DD1;
        case StorageCategory::Ffmpeg:    return 0x6CA642;
        case StorageCategory::Assets:    return 0x9B6BD1;
        case StorageCategory::Pdb:       return 0xD15B5B;
        case StorageCategory::Program:   return 0xD19B3B;
        case StorageCategory::Runtime:   return 0x4FB0A5;
        case StorageCategory::Config:    return 0x8A8FA3;
        case StorageCategory::Logs:      return 0xB08A3B;
        default:                         return 0x6E6E6E;
    }
}

// 内存页只展示"我们自己的"模块：自写的 EXE / 三个功能 DLL，以及随包的 ffmpeg 解码库。
// 系统公共 DLL（System32、显卡驱动、WinSxS）、第三方框架（Windows App SDK / onnxruntime /
// DirectML / WebView2Loader）一律排除 —— 它们不是本程序的资源。
inline bool is_own_module(const std::wstring& modulePath, const std::wstring& exeDir) {
    using namespace detail;

    const std::wstring name = lower(name_of(modulePath));
    if (name == L"desktop_sticker.exe" || name == L"desktopsticker.features.dll" ||
        name == L"desktopsticker.wallpaper.dll" || name == L"desktopsticker.resmon.dll") {
        return true;
    }

    // ffmpeg 解码库：既要求 av*/sw* 前缀，又要求确实来自我们的 ffmpeg 负载目录，
    // 避免把系统里同前缀的库误判成随包负载。
    if (lower(ext_of(modulePath)) != L".dll") return false;
    const bool ffmpegLike = name.rfind(L"av", 0) == 0 || name.rfind(L"sw", 0) == 0;
    if (!ffmpegLike) return false;
    return is_under(modulePath, trim_sep(exeDir) + L"\\ffmpeg");
}

} // namespace desktopsticker::resmon
