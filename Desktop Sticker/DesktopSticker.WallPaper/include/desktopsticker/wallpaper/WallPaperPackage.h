#pragma once

// .dswall 壁纸包（ZIP 容器）的解析与校验：纯函数，只依赖 nlohmann/json 与标准库。
// 包内容约定（由编辑器插件 addons/dstk_wallpaper 生成）：
//   project.pck    Godot 导出包
//   manifest.json  标题 / 主场景 / 引擎指纹 / 请求的能力 / 格式版本
//   poster.png     缩略图（可选）
// 本头文件被 WallPaper DLL 与 dtest 共用；不得引入 windows.h。

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace desktopsticker::wallpaper {

inline constexpr int kWallPaperPackFormatVersion = 1;

inline constexpr wchar_t kWallPaperPackExt[] = L".dswall";
inline constexpr wchar_t kPackEntryPack[] = L"project.pck";
inline constexpr wchar_t kPackEntryManifest[] = L"manifest.json";
inline constexpr wchar_t kPackEntryPoster[] = L"poster.png";

struct WallPaperManifest {
    int formatVersion = 0;
    std::string title;        // UTF-8
    std::string mainScene;    // 例如 res://main.tscn
    std::string engineVersion; // 例如 4.7.2
    std::string engineBuild;   // 例如 4.7.2.stable (custom_build)
};

// 解析 manifest.json 文本。失败返回 false 并填 error（中文，供日志/UI 用）。
inline bool parse_wallpaper_manifest(const std::string& text, WallPaperManifest& out, std::string& error) {
    const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        error = "manifest.json 不是合法的 JSON 对象";
        return false;
    }
    out.formatVersion = j.value("format_version", 0);
    if (out.formatVersion != kWallPaperPackFormatVersion) {
        error = "不支持的壁纸包格式版本：" + std::to_string(out.formatVersion);
        return false;
    }
    out.title = j.value("title", std::string{});
    out.mainScene = j.value("main_scene", std::string{});
    if (out.mainScene.empty()) {
        error = "manifest 缺少 main_scene";
        return false;
    }
    if (j.contains("engine") && j["engine"].is_object()) {
        out.engineVersion = j["engine"].value("version", std::string{});
        out.engineBuild = j["engine"].value("build", std::string{});
    }
    return true;
}

// 目录清单里有没有 project.pck（大小写不敏感）。纯判断，用于识别"已解包的壁纸包目录"。
inline bool has_wallpaper_pack_entry(const std::vector<std::wstring>& entries) {
    for (const auto& e : entries) {
        std::wstring lower = e;
        for (wchar_t& c : lower) {
            if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
        }
        if (lower == kPackEntryPack) return true;
    }
    return false;
}

} // namespace desktopsticker::wallpaper
