#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "desktopsticker/ConfigStore.h"
#include "desktopsticker/Export.h"
#include "desktopsticker/PinyinMapper.h"
#include "desktopsticker/SearchSources.h"

namespace desktopsticker {

struct DESKTOPSTICKER_API IndexedItem {
    std::wstring name;
    std::wstring path;
    std::wstring source; // Desktop / Documents / Downloads / Pictures / Videos / Music / Apps
    std::wstring pinyin;
    bool isApp = false;
};

class DESKTOPSTICKER_API IndexService {
public:
    explicit IndexService(ConfigStore* config);

    bool Rebuild();
    std::vector<IndexedItem> Search(const std::wstring& query, size_t maxResults) const;

    bool AddApp(const std::wstring& path);
    bool RemoveApp(const std::wstring& path);

    const std::vector<IndexedItem>& Items() const { return items_; }
    const std::vector<std::wstring>& Apps() const { return apps_; }

private:
    void ScanDirectory(const std::filesystem::path& dir, const std::wstring& source);
    void ScanStartMenuDir(const std::filesystem::path& dir, int depth);
    void ScanKnownFolders();
    void ScanDesktop();
    void ScanStartMenu();
    void LoadApps();
    bool SaveApps() const;

    ConfigStore* config_;
    std::filesystem::path rootDir_;
    std::filesystem::path appsPath_;
    std::vector<IndexedItem> items_;
    std::vector<std::wstring> apps_;
};

} // namespace desktopsticker
