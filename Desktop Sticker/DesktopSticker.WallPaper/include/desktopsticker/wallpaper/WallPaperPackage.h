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

    // 以下为可选字段（format_version 仍为 1：新字段缺失时取空，旧包照旧能导入）。
    std::string author;
    std::string description;
    std::string packId;                 // 稳定作者标识，热应用去重用
    std::vector<std::string> tags;
    std::vector<std::string> categories;
    std::vector<std::string> requestedPermissions; // 能力 id：network/file_outside/exec/system
};

// 读一个字符串数组字段；缺省/非数组/元素非字符串时跳过，不报错。
inline void read_string_array(const nlohmann::json& j, const char* key,
                              std::vector<std::string>& out) {
    if (!j.contains(key) || !j[key].is_array()) return;
    for (const auto& e : j[key]) {
        if (e.is_string()) out.push_back(e.get<std::string>());
    }
}

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
    // 可选作者元数据：缺失即空，绝不因缺字段判失败。
    out.author = j.value("author", std::string{});
    out.description = j.value("description", std::string{});
    out.packId = j.value("pack_id", std::string{});
    read_string_array(j, "tags", out.tags);
    read_string_array(j, "categories", out.categories);
    read_string_array(j, "requested_permissions", out.requestedPermissions);
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
