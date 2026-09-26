#include "pch.h"
#include "WallPaperStore.h"

#include "Log.h"
#include "Utf8.h"
#include "desktopsticker/wallpaper/BackendKind.h"
#include "desktopsticker/wallpaper/DecodePath.h"

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using nlohmann::json;

namespace desktopsticker::wallpaper {

namespace {

constexpr int kLibraryVersion = 2;   // v2：条目新增 kind 字段
constexpr int kStateVersion = 2;   // v2：补上速度/音频/解码路径（旧文件按默认值读入）

const char* variant_to_string(VariantKind k) {
    switch (k) {
        case VariantKind::Balanced:   return "balanced";
        case VariantKind::PowerSaver: return "power-saver";
        default:                      return "original";
    }
}

VariantKind variant_from_string(const std::string& s) {
    if (s == "balanced") return VariantKind::Balanced;
    if (s == "power-saver") return VariantKind::PowerSaver;
    return VariantKind::Original;
}

// 损坏时把原文件改名备份，避免用户数据被静默丢弃
void backup_corrupt(const std::wstring& path) {
    std::error_code ec;
    const fs::path p(path);
    if (!fs::exists(p, ec)) return;
    const fs::path bak = p.wstring() + L".bak";
    fs::remove(bak, ec);
    fs::rename(p, bak, ec);
    if (ec) wp_log("backup corrupt failed: " + to_utf8(path));
    else    wp_log("corrupt file backed up: " + to_utf8(path));
}

std::string read_all(const std::wstring& path, bool& ok) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { ok = false; return {}; }
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ok = true;
    return s;
}

} // namespace

bool WriteFileAtomic(const std::wstring& path, const std::string& content) {
    std::error_code ec;
    const fs::path target(path);
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);

    const fs::path tmp = target.wstring() + L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!out) return false;
    }
    if (!MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

WallPaperStore::WallPaperStore(std::wstring configDir) : configDir_(std::move(configDir)) {}

PersistedState WallPaperStore::LoadState() const {
    PersistedState state;
    const std::wstring path = (fs::path(configDir_) / L"wallpaper.json").wstring();

    bool ok = false;
    const std::string raw = read_all(path, ok);
    if (!ok) return state; // 缺失 → 默认值

    try {
        const json j = json::parse(raw);
        if (j.contains("settings")) {
            const auto& s = j["settings"];
            state.settings.enabled = s.value("enabled", false);
            state.settings.activeId = from_utf8(s.value("activeId", std::string{}));
            state.settings.preferred = variant_from_string(s.value("preferred", "original"));
            state.settings.pauseOnFullscreen = s.value("pauseOnFullscreen", true);
            state.settings.pauseOnLock = s.value("pauseOnLock", true);
            // 这三个曾经漏存，导致设置页里的速度/声音/音量重启即丢
            state.settings.speed = s.value("speed", 1.0);
            state.settings.audioEnabled = s.value("audioEnabled", false);
            state.settings.audioVolume = s.value("audioVolume", 1.0f);
            state.settings.decodePath =
                decode_path_from_string(s.value("decodePath", std::string("auto")));
        }
        if (j.contains("storage")) {
            const auto& st = j["storage"];
            state.storage.root = from_utf8(st.value("root", std::string{}));
            state.storage.volumeSerial = st.value("volumeSerial", 0u);
            state.storage.rootFileId = st.value("rootFileId", 0ull);
            state.storage.bound = st.value("bound", false);
        }
    } catch (const std::exception& e) {
        wp_log(std::string("wallpaper.json parse failed: ") + e.what());
        backup_corrupt(path);
        return PersistedState{};
    }
    state.settings.libraryRoot = state.storage.root;
    return state;
}

bool WallPaperStore::SaveState(const PersistedState& state) const {
    json j;
    j["version"] = kStateVersion;
    j["settings"] = {
        {"enabled", state.settings.enabled},
        {"activeId", to_utf8(state.settings.activeId)},
        {"preferred", variant_to_string(state.settings.preferred)},
        {"pauseOnFullscreen", state.settings.pauseOnFullscreen},
        {"pauseOnLock", state.settings.pauseOnLock},
        {"speed", state.settings.speed},
        {"audioEnabled", state.settings.audioEnabled},
        {"audioVolume", state.settings.audioVolume},
        {"decodePath", decode_path_to_string(state.settings.decodePath)},
    };
    j["storage"] = {
        {"root", to_utf8(state.storage.root)},
        {"volumeSerial", state.storage.volumeSerial},
        {"rootFileId", state.storage.rootFileId},
        {"bound", state.storage.bound},
    };
    const std::wstring path = (fs::path(configDir_) / L"wallpaper.json").wstring();
    return WriteFileAtomic(path, j.dump(2));
}

std::vector<WallPaperItem> WallPaperStore::LoadLibrary(const std::wstring& libraryRoot) const {
    std::vector<WallPaperItem> items;
    const std::wstring path = (fs::path(libraryRoot) / L"library.json").wstring();

    bool ok = false;
    const std::string raw = read_all(path, ok);
    if (!ok) return items;

    try {
        const json j = json::parse(raw);
        if (!j.contains("items") || !j["items"].is_array()) return items;
        for (const auto& e : j["items"]) {
            WallPaperItem it;
            it.id = from_utf8(e.value("id", std::string{}));
            it.name = from_utf8(e.value("name", std::string{}));
            it.sourceFile = from_utf8(e.value("sourceFile", std::string{}));
            // v1 数据没有 kind 字段：旧数据只可能是视频，缺失即回落 Video（自动迁移）
            it.kind = backend_kind_from_id(from_utf8(e.value("kind", std::string("video"))));
            it.hasPoster = e.value("hasPoster", false);
            it.hasBalanced = e.value("hasBalanced", false);
            it.hasPowerSaver = e.value("hasPowerSaver", false);
            it.sourceBytes = e.value("sourceBytes", 0ull);
            if (!it.id.empty()) items.push_back(std::move(it));
        }
    } catch (const std::exception& e) {
        wp_log(std::string("library.json parse failed: ") + e.what());
        backup_corrupt(path);
        return {};
    }
    return items;
}

bool WallPaperStore::SaveLibrary(const std::wstring& libraryRoot,
                                 const std::vector<WallPaperItem>& items) const {
    json arr = json::array();
    for (const auto& it : items) {
        arr.push_back({
            {"id", to_utf8(it.id)},
            {"name", to_utf8(it.name)},
            {"sourceFile", to_utf8(it.sourceFile)},
            {"kind", to_utf8(backend_kind_id(it.kind))},
            {"hasPoster", it.hasPoster},
            {"hasBalanced", it.hasBalanced},
            {"hasPowerSaver", it.hasPowerSaver},
            {"sourceBytes", it.sourceBytes},
        });
    }
    json j;
    j["version"] = kLibraryVersion;
    j["items"] = std::move(arr);

    const std::wstring path = (fs::path(libraryRoot) / L"library.json").wstring();
    return WriteFileAtomic(path, j.dump(2));
}

} // namespace desktopsticker::wallpaper
