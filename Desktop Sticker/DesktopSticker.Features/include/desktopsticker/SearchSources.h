#pragma once

namespace desktopsticker {

// 搜索结果来源标识：DLL 侧（IndexService）写入 SearchResult.source，
// EXE 侧（LauncherController）按此分组展示。
// 这是跨 DLL 边界的契约字符串，随 IFeatureModule.h 一起暴露给 EXE；
// 双方都必须引用这里，不得各自硬编码字面量。
namespace sources {
inline constexpr wchar_t kApps[] = L"Apps";
inline constexpr wchar_t kStartMenu[] = L"StartMenu";
inline constexpr wchar_t kDesktop[] = L"Desktop";
inline constexpr wchar_t kDocuments[] = L"Documents";
inline constexpr wchar_t kDownloads[] = L"Downloads";
inline constexpr wchar_t kPictures[] = L"Pictures";
inline constexpr wchar_t kVideos[] = L"Videos";
inline constexpr wchar_t kMusic[] = L"Music";
} // namespace sources

} // namespace desktopsticker
