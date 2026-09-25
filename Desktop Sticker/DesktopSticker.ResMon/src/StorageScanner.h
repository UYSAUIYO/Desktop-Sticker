#pragma once

#include "desktopsticker/IResMonModule.h"
#include "desktopsticker/resmon/JsonBuild.h"

namespace desktopsticker::resmon {

// 只读存储统计：遍历三个根，按 Classify.h 的 9 条有序规则归类累加。
// 只做 file_size / 目录遍历，不创建、不修改、不删除任何文件。
// 同步执行；调用方负责放到工作线程（扫描大壁纸库可能耗时）。
class StorageScanner {
public:
    StorageSnapshot Scan(const desktopsticker::ResMonPaths& paths);

private:
    void scan_root(const std::wstring& root, const ClassifyRoots& roots,
                   const wchar_t* label, StorageSnapshot& out);
};

} // namespace desktopsticker::resmon
