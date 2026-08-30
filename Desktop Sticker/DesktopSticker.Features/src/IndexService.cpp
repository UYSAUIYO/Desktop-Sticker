#include "pch.h"
#include "desktopsticker/IndexService.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>

#include "desktopsticker/FileUtil.h"
#include "desktopsticker/KnownFolders.h"
#include "desktopsticker/StringUtil.h"
#include "desktopsticker/Utf8.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace desktopsticker {

namespace {

bool IsHidden(const fs::path& p) {
    const DWORD attrs = GetFileAttributesW(p.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_HIDDEN);
}

int MatchScore(const std::wstring& nameLower, const std::wstring& pinyinLower, const std::wstring& queryLower) {
    if (nameLower.rfind(queryLower, 0) == 0) return 0;       // 名称前缀
    if (nameLower.find(queryLower) != std::wstring::npos) return 1;  // 名称包含
    if (pinyinLower.rfind(queryLower, 0) == 0) return 2;     // 拼音前缀
    if (pinyinLower.find(queryLower) != std::wstring::npos) return 3; // 拼音包含
    return -1;
}

} // namespace

IndexService::IndexService(ConfigStore* config)
    : config_(config),
      rootDir_(config->GetRootDir()),
      appsPath_(rootDir_ / L"apps.json") {}

bool IndexService::Rebuild() {
    items_.clear();
    LoadApps();

    const AppConfig& cfg = config_->GetConfig();
    if (cfg.searchDesktop) ScanDesktop();
    if (cfg.searchKnownFolders) ScanKnownFolders();
    if (cfg.searchStartMenu) ScanStartMenu();

    for (const auto& app : apps_) {
        IndexedItem item;
        item.path = app;
        item.name = fs::path(app).filename().wstring();
        if (item.name.empty()) item.name = app;
        item.source = sources::kApps;
        item.pinyin = PinyinMapper::GetInitials(item.name);
        item.isApp = true;
        items_.push_back(std::move(item));
    }

    std::sort(items_.begin(), items_.end(),
              [](const IndexedItem& a, const IndexedItem& b) { return a.name < b.name; });
    return true;
}

void IndexService::ScanDesktop() {
    const auto app = desktopsticker::GetKnownPath(FOLDERID_Desktop);
    if (!app.empty()) {
        ScanDirectory(app, sources::kDesktop);
    }
}

void IndexService::ScanKnownFolders() {
    struct Folder { KNOWNFOLDERID id; const wchar_t* name; };
    const Folder folders[] = {
        {FOLDERID_Documents, sources::kDocuments},
        {FOLDERID_Downloads, sources::kDownloads},
        {FOLDERID_Pictures, sources::kPictures},
        {FOLDERID_Videos, sources::kVideos},
        {FOLDERID_Music, sources::kMusic},
    };
    for (const auto& f : folders) {
        const std::wstring path = GetKnownPath(f.id);
        if (!path.empty()) ScanDirectory(path, f.name);
    }
}

void IndexService::ScanStartMenu() {
    // 用户与公共开始菜单是已安装应用快捷方式的主要来源（如 QQ/微信的 .lnk）
    const std::wstring user = GetKnownPath(FOLDERID_StartMenu);
    const std::wstring common = GetKnownPath(FOLDERID_CommonStartMenu);
    if (!user.empty()) ScanStartMenuDir(fs::path(user) / L"Programs", 0);
    if (!common.empty()) ScanStartMenuDir(fs::path(common) / L"Programs", 0);
}

void IndexService::ScanStartMenuDir(const std::filesystem::path& dir, int depth) {
    std::error_code ec;
    if (!fs::exists(dir, ec)) return;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        const auto& p = entry.path();
        if (entry.is_directory(ec)) {
            if (depth < 3) ScanStartMenuDir(p, depth + 1);
            continue;
        }
        const std::wstring extLower = ToLowerCopy(p.extension().wstring());
        if (extLower != L".lnk" && extLower != L".url") continue;
        IndexedItem item;
        item.name = p.stem().wstring(); // 快捷方式显示为去扩展名的名称
        item.path = p.wstring();
        item.source = sources::kStartMenu;
        item.pinyin = PinyinMapper::GetInitials(item.name);
        item.isApp = true;
        items_.push_back(std::move(item));
    }
}

void IndexService::ScanDirectory(const fs::path& dir, const std::wstring& source) {
    std::error_code ec;
    if (!fs::exists(dir, ec)) return;

    const bool includeHidden = config_->GetConfig().includeHiddenFiles;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        const auto& p = entry.path();
        if (!includeHidden && IsHidden(p)) continue;

        IndexedItem item;
        item.name = p.filename().wstring();
        item.path = p.wstring();
        item.source = source;
        item.pinyin = PinyinMapper::GetInitials(item.name);
        items_.push_back(std::move(item));
    }
}

void IndexService::LoadApps() {
    apps_.clear();
    std::error_code ec;
    if (!fs::exists(appsPath_, ec)) return;
    try {
        std::ifstream in(appsPath_);
        json j;
        in >> j;
        for (const auto& app : j.value("apps", json::array())) {
            apps_.push_back(FromUtf8(app.get<std::string>()));
        }
    } catch (...) {
        apps_.clear();
    }
}

bool IndexService::SaveApps() const {
    json j = json::array();
    for (const auto& app : apps_) {
        j.push_back(ToUtf8(app));
    }
    json root;
    root["apps"] = j;
    return WriteFileAtomic(appsPath_, root.dump(2));
}

bool IndexService::AddApp(const std::wstring& path) {
    for (const auto& app : apps_) {
        if (_wcsicmp(app.c_str(), path.c_str()) == 0) return true;
    }
    apps_.push_back(path);
    return SaveApps();
}

bool IndexService::RemoveApp(const std::wstring& path) {
    auto it = std::remove_if(apps_.begin(), apps_.end(),
                             [&](const std::wstring& app) { return _wcsicmp(app.c_str(), path.c_str()) == 0; });
    if (it == apps_.end()) return false;
    apps_.erase(it, apps_.end());
    return SaveApps();
}

std::vector<IndexedItem> IndexService::Search(const std::wstring& query, size_t maxResults) const {
    if (query.empty()) return {};

    const std::wstring queryLower = ToLowerCopy(query);
    std::vector<std::pair<int, const IndexedItem*>> scored;

    for (const auto& item : items_) {
        const std::wstring nameLower = ToLowerCopy(item.name);
        const std::wstring pinyinLower = ToLowerCopy(item.pinyin);
        const int score = MatchScore(nameLower, pinyinLower, queryLower);
        if (score >= 0) {
            scored.emplace_back(score, &item);
        }
    }

    std::stable_sort(scored.begin(), scored.end(),
                     [](const auto& a, const auto& b) {
                         if (a.first != b.first) return a.first < b.first;
                         return a.second->name < b.second->name;
                     });

    std::vector<IndexedItem> results;
    results.reserve(std::min(maxResults, scored.size()));
    for (size_t i = 0; i < scored.size() && i < maxResults; ++i) {
        results.push_back(*scored[i].second);
    }
    return results;
}

} // namespace desktopsticker
