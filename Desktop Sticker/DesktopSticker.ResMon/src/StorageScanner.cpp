#include "pch.h"
#include "StorageScanner.h"

#include "desktopsticker/resmon/Classify.h"
#include "desktopsticker/resmon/Format.h"

#include <set>

namespace fs = std::filesystem;

namespace desktopsticker::resmon {

namespace {

int64_t now_unix_ms() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    const int64_t ticks = (static_cast<int64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return (ticks - 116444736000000000LL) / 10000LL; // 1601-01-01 → Unix epoch
}

} // namespace

void StorageScanner::scan_root(const std::wstring& root, const ClassifyRoots& roots,
                               const wchar_t* label, StorageSnapshot& out) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        std::wstring msg = std::wstring(L"[") + label + L"] 目录不存在或不可读: " + root;
        if (ec) msg += L" (" + from_utf8(ec.message()) + L")";
        out.warnings.push_back(std::move(msg));
        return;
    }

    // 同一类错误只保留一条，避免上千条重复淹没页面
    std::set<std::wstring> seen;

    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        out.warnings.push_back(std::wstring(L"[") + label + L"] 无法开始扫描: " + root);
        return;
    }

    const fs::recursive_directory_iterator end;
    while (it != end) {
        std::error_code fileEc;
        if (it->is_regular_file(fileEc)) {
            const uint64_t size = it->file_size(fileEc);
            if (!fileEc) {
                const StorageCategory c = classify_path(it->path().wstring(), roots);
                for (auto& row : out.rows) {
                    if (row.category == c) {
                        row.bytes += size;
                        break;
                    }
                }
            } else {
                seen.insert(L"部分文件大小读取失败（已跳过）");
            }
        }

        // 用 error_code 版本推进：不抛异常；失败时迭代器不再解引用
        it.increment(ec);
        if (ec) {
            seen.insert(L"遍历中断（已跳过剩余内容）");
            ec.clear();
            break;
        }
    }

    for (const auto& w : seen) out.warnings.push_back(w);
}

StorageSnapshot StorageScanner::Scan(const desktopsticker::ResMonPaths& paths) {
    StorageSnapshot out;
    out.scannedAtMs = now_unix_ms();

    // 固定 9 类，保持枚举顺序，保证前端列顺序稳定
    for (int i = 0; i < static_cast<int>(StorageCategory::Count); ++i) {
        StorageRow row;
        row.category = static_cast<StorageCategory>(i);
        out.rows.push_back(row);
    }

    ClassifyRoots roots;
    roots.exeDir = paths.exeDir;
    roots.configDir = paths.configDir;
    roots.wallpaperRoot = paths.wallpaperRoot;

    if (paths.wallpaperRoot.empty()) {
        out.warnings.push_back(L"壁纸库路径为空（壁纸模块不可用），已跳过壁纸媒体库统计");
    }

    scan_root(paths.exeDir, roots, L"程序目录", out);

    // configDir 一般不在 exeDir 下；若在，跳过以免重复计数
    if (detail::is_under(paths.configDir, paths.exeDir)) {
        out.warnings.push_back(L"配置目录位于程序目录内，已并入程序目录统计以免重复计数");
    } else {
        scan_root(paths.configDir, roots, L"配置目录", out);
    }

    if (!paths.wallpaperRoot.empty()) {
        scan_root(paths.wallpaperRoot, roots, L"壁纸库", out);
    }

    std::vector<uint64_t> bytes;
    bytes.reserve(out.rows.size());
    for (const auto& row : out.rows) {
        bytes.push_back(row.bytes);
        out.totalBytes += row.bytes;
    }

    const auto percents = compute_percentages(bytes);
    for (size_t i = 0; i < out.rows.size(); ++i) out.rows[i].percent = percents[i];

    return out;
}

} // namespace desktopsticker::resmon
