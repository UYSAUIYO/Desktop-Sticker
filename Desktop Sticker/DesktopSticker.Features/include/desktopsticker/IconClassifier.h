#pragma once
#include <string>
#include <vector>

#include "desktopsticker/Export.h"

namespace desktopsticker {

// 桌面图标自动分类：关键词表驱动、纯判定逻辑（可单测）。
// 修改分类规则后必须递增 layout.json 的 classVersion（DesktopWorkspace 里的 kClassVersion），
// 旧布局才会自动重新分类。
class DESKTOPSTICKER_API IconClassifier {
public:
    struct CategoryRules {
        const wchar_t* category;
        std::vector<std::wstring> keywords; // 全小写，按"包含"匹配；表序即优先级
    };

    // 应用类关键词规则（顺序敏感：越靠前优先级越高）
    static const std::vector<CategoryRules>& AppRules();

    // 按完整路径判定分类；目录走目录名规则，非应用文件归"其他"，未命中关键词的应用归"应用"
    std::wstring ClassifyPath(const std::wstring& path) const;
};

} // namespace desktopsticker
