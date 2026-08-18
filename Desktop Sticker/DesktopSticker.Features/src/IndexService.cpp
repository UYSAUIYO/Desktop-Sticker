#include "pch.h"
#include "desktopsticker/IndexService.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>

#include "desktopsticker/Utf8.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace desktopsticker {

namespace {

std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return std::towlower(c); });
    return s;
}

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

    for (const auto& app : apps_) {
        IndexedItem item;
        item.path = app;
        item.name = fs::path(app).filename().wstring();
        if (item.name.empty()) item.name = app;
        item.source = L"Apps";
        item.pinyin = PinyinMapper::GetInitials(item.name);
        item.isApp = true;
        items_.push_back(std::move(item));
    }

    std::sort(items_.begin(), items_.end(),
              [](const IndexedItem& a, const IndexedItem& b) { return a.name < b.name; });
    return true;
}

void IndexService::ScanDesktop() {
    PWSTR desktopPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktopPath))) {
        ScanDirectory(desktopPath, L"Desktop");
        CoTaskMemFree(desktopPath);
    }
}

void IndexService::ScanKnownFolders() {
    struct Folder { KNOWNFOLDERID id; const wchar_t* name; };
    const Folder folders[] = {
        {FOLDERID_Documents, L"Documents"},
        {FOLDERID_Downloads, L"Downloads"},
        {FOLDERID_Pictures, L"Pictures"},
        {FOLDERID_Videos, L"Videos"},
        {FOLDERID_Music, L"Music"},
    };
    for (const auto& f : folders) {
        PWSTR path = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(f.id, 0, nullptr, &path))) {
            ScanDirectory(path, f.name);
            CoTaskMemFree(path);
        }
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

    std::error_code ec;
    fs::create_directories(rootDir_, ec);
    const fs::path tmp = appsPath_.wstring() + L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        out << root.dump(2);
        out.flush();
    }
    return MoveFileExW(tmp.c_str(), appsPath_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
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

    const std::wstring queryLower = ToLower(query);
    std::vector<std::pair<int, const IndexedItem*>> scored;

    for (const auto& item : items_) {
        const std::wstring nameLower = ToLower(item.name);
        const std::wstring pinyinLower = ToLower(item.pinyin);
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
