#pragma once

// 快照结构体 + 纯函数式响应组装器。
// 组装器只吃纯数据结构、不碰任何系统调用，因此可直接用 dtest 覆盖；
// JSON 转义由已有的 nlohmann/json 保证（比自己手写转义安全）。

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "desktopsticker/resmon/Classify.h"

#include <nlohmann/json.hpp>

namespace desktopsticker::resmon {

inline std::string to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int need = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (need <= 0) return {};
    std::string out(static_cast<size_t>(need), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                        out.data(), need, nullptr, nullptr);
    return out;
}

inline std::wstring from_utf8(const std::string& s) {
    if (s.empty()) return {};
    const int need = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                         nullptr, 0);
    if (need <= 0) return {};
    std::wstring out(static_cast<size_t>(need), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), need);
    return out;
}

struct ThreadRow {
    uint32_t tid = 0;
    std::wstring name;
    double cpuPercent = 0.0;
    int64_t totalMs = 0;
    bool baseline = false;
};

struct ChildRow {
    uint32_t pid = 0;
    std::wstring name;
    double cpuPercent = 0.0;
    uint64_t workingSetBytes = 0;
};

struct CpuSnapshot {
    double totalPercent = 0.0;
    std::vector<ThreadRow> threads;
    std::vector<ChildRow> children;
    bool baseline = false;
};

struct ModuleRow {
    std::wstring name;
    uint64_t imageBytes = 0;
};

struct MemorySnapshot {
    uint64_t workingSetBytes = 0;
    uint64_t privateBytes = 0;
    uint64_t peakWorkingSetBytes = 0;
    std::vector<ModuleRow> modules;
    std::vector<ChildRow> children;
};

struct StorageRow {
    StorageCategory category = StorageCategory::Other;
    uint64_t bytes = 0;
    double percent = 0.0;
};

struct StorageSnapshot {
    uint64_t totalBytes = 0;
    std::vector<StorageRow> rows;
    std::vector<std::wstring> warnings;
    int64_t scannedAtMs = 0;
};

namespace detail {
inline std::string hex_color(uint32_t rgb) {
    char buf[16]{};
    snprintf(buf, sizeof(buf), "#%06X", rgb & 0xFFFFFFu);
    return buf;
}

inline nlohmann::json child_to_json(const ChildRow& c) {
    return nlohmann::json{
        {"pid", c.pid},
        {"name", to_utf8(c.name)},
        {"cpuPercent", c.cpuPercent},
        {"workingSetBytes", c.workingSetBytes},
    };
}
} // namespace detail

inline std::string build_cpu_response(const CpuSnapshot& s, int64_t sampledAtMs) {
    nlohmann::json threads = nlohmann::json::array();
    for (const auto& t : s.threads) {
        threads.push_back({
            {"tid", t.tid},
            // 未命名线程给一个可读占位，前端无需处理空值
            {"name", to_utf8(t.name.empty() ? (L"线程 " + std::to_wstring(t.tid)) : t.name)},
            {"cpuPercent", t.cpuPercent},
            {"totalMs", t.totalMs},
        });
    }
    nlohmann::json children = nlohmann::json::array();
    for (const auto& c : s.children) children.push_back(detail::child_to_json(c));

    nlohmann::json j{
        {"type", "cpu"},
        {"sampledAtMs", sampledAtMs},
        {"totalPercent", s.totalPercent},
        {"baseline", s.baseline},
        {"threads", std::move(threads)},
        {"children", std::move(children)},
    };
    return j.dump();
}

inline std::string build_memory_response(const MemorySnapshot& s) {
    nlohmann::json modules = nlohmann::json::array();
    for (const auto& m : s.modules) {
        modules.push_back({{"name", to_utf8(m.name)}, {"imageBytes", m.imageBytes}});
    }
    nlohmann::json children = nlohmann::json::array();
    for (const auto& c : s.children) children.push_back(detail::child_to_json(c));

    nlohmann::json j{
        {"type", "memory"},
        {"workingSetBytes", s.workingSetBytes},
        {"privateBytes", s.privateBytes},
        {"peakWorkingSetBytes", s.peakWorkingSetBytes},
        {"modules", std::move(modules)},
        {"children", std::move(children)},
    };
    return j.dump();
}

inline std::string build_storage_response(const StorageSnapshot& s) {
    nlohmann::json categories = nlohmann::json::array();
    for (const auto& r : s.rows) {
        nlohmann::json item{
            {"id", to_utf8(category_id(r.category))},
            {"name", to_utf8(category_name(r.category))},
            {"bytes", r.bytes},
            {"percent", r.percent},
            {"color", detail::hex_color(category_color(r.category))},
        };
        if (const wchar_t* note = category_note(r.category)) item["note"] = to_utf8(note);
        categories.push_back(std::move(item));
    }
    nlohmann::json warnings = nlohmann::json::array();
    for (const auto& w : s.warnings) warnings.push_back(to_utf8(w));

    nlohmann::json j{
        {"type", "storage"},
        {"scannedAtMs", s.scannedAtMs},
        {"totalBytes", s.totalBytes},
        {"categories", std::move(categories)},
        {"warnings", std::move(warnings)},
    };
    return j.dump();
}

inline std::string build_error_response(const std::wstring& cmd, const std::wstring& message) {
    nlohmann::json j{
        {"type", "error"},
        {"cmd", to_utf8(cmd)},
        {"message", to_utf8(message)},
    };
    return j.dump();
}

} // namespace desktopsticker::resmon
