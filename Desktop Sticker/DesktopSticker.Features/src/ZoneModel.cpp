#include "pch.h"
#include "desktopsticker/ZoneModel.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "desktopsticker/Utf8.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace desktopsticker {

namespace {
json RectToJson(const RECT& r) {
    return json{{"left", r.left}, {"top", r.top}, {"right", r.right}, {"bottom", r.bottom}};
}
RECT RectFromJson(const json& j) {
    RECT r{};
    r.left = j.value("left", 0);
    r.top = j.value("top", 0);
    r.right = j.value("right", 100);
    r.bottom = j.value("bottom", 200);
    return r;
}
} // namespace

bool ZoneModel::Load(const fs::path& layoutPath) {
    if (!fs::exists(layoutPath)) return false;
    try {
        std::ifstream in(layoutPath);
        json j;
        in >> j;

        DesktopLayout layout;
        layout.autoArrangeWasEnabled = j.value("autoArrangeWasEnabled", false);

        for (const auto& zj : j.value("zones", json::array())) {
            Zone z;
            z.id = FromUtf8(zj.value("id", ""));
            z.name = FromUtf8(zj.value("name", ""));
            z.rect = RectFromJson(zj.value("rect", json::object()));
            z.monitorIndex = zj.value("monitorIndex", 0);
            z.collapsed = zj.value("collapsed", false);
            for (const auto& p : zj.value("itemPaths", json::array())) {
                z.itemPaths.push_back(FromUtf8(p.get<std::string>()));
            }
            layout.zones.push_back(std::move(z));
        }

        const json positions = j.value("originalIconPositions", json::object());
        for (auto it = positions.begin(); it != positions.end(); ++it) {
            const auto& value = it.value();
            POINT pt{};
            pt.x = value.value("x", 0);
            pt.y = value.value("y", 0);
            layout.originalIconPositions[FromUtf8(it.key())] = pt;
        }

        layout_ = std::move(layout);
        return true;
    } catch (...) {
        return false;
    }
}

bool ZoneModel::Save(const fs::path& layoutPath) const {
    json j;
    j["autoArrangeWasEnabled"] = layout_.autoArrangeWasEnabled;

    json zones = json::array();
    for (const auto& z : layout_.zones) {
        json zj;
        zj["id"] = ToUtf8(z.id);
        zj["name"] = ToUtf8(z.name);
        zj["rect"] = RectToJson(z.rect);
        zj["monitorIndex"] = z.monitorIndex;
        zj["collapsed"] = z.collapsed;
        json paths = json::array();
        for (const auto& p : z.itemPaths) paths.push_back(ToUtf8(p));
        zj["itemPaths"] = paths;
        zones.push_back(std::move(zj));
    }
    j["zones"] = zones;

    json positions = json::object();
    for (const auto& [path, pt] : layout_.originalIconPositions) {
        positions[ToUtf8(path)] = json{{"x", pt.x}, {"y", pt.y}};
    }
    j["originalIconPositions"] = positions;

    std::error_code ec;
    fs::create_directories(layoutPath.parent_path(), ec);
    const fs::path tmp = layoutPath.wstring() + L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        out << j.dump(2);
        out.flush();
    }
    return MoveFileExW(tmp.c_str(), layoutPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

Zone* ZoneModel::FindZone(const std::wstring& id) {
    for (auto& z : layout_.zones) {
        if (z.id == id) return &z;
    }
    return nullptr;
}

Zone* ZoneModel::FindZoneByName(const std::wstring& name) {
    for (auto& z : layout_.zones) {
        if (z.name == name) return &z;
    }
    return nullptr;
}

void ZoneModel::AddZone(Zone zone) {
    if (zone.id.empty()) zone.id = GenerateZoneId();
    layout_.zones.push_back(std::move(zone));
}

bool ZoneModel::RemoveZone(const std::wstring& id) {
    auto it = std::remove_if(layout_.zones.begin(), layout_.zones.end(),
                             [&](const Zone& z) { return z.id == id; });
    if (it == layout_.zones.end()) return false;
    layout_.zones.erase(it, layout_.zones.end());
    return true;
}

bool ZoneModel::MoveItem(const std::wstring& itemPath, const std::wstring& fromZoneId, const std::wstring& toZoneId) {
    Zone* from = FindZone(fromZoneId);
    Zone* to = FindZone(toZoneId);
    if (!from || !to || fromZoneId == toZoneId) return false;

    auto it = std::find(from->itemPaths.begin(), from->itemPaths.end(), itemPath);
    if (it == from->itemPaths.end()) return false;
    from->itemPaths.erase(it);
    to->itemPaths.push_back(itemPath);
    return true;
}

std::wstring ZoneModel::GenerateZoneId() const {
    GUID guid{};
    CoCreateGuid(&guid);
    wchar_t buf[64];
    StringFromGUID2(guid, buf, 64);
    return std::wstring(buf);
}

} // namespace desktopsticker
