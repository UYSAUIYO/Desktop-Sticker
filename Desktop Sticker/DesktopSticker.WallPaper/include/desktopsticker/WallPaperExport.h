#pragma once

// 注意：文件名刻意不叫 Export.h —— 那会与 Features 的 desktopsticker/Export.h
// 在同一个包含路径上冲突（EXE 同时包含两个 DLL 的头），#pragma once 会让后包含的
// 那个被跳过、宏永远不定义。名字必须唯一。

#ifdef DESKTOPSTICKER_WALLPAPER_BUILD
#define DESKTOPSTICKER_WALLPAPER_API __declspec(dllexport)
#else
#define DESKTOPSTICKER_WALLPAPER_API __declspec(dllimport)
#endif
