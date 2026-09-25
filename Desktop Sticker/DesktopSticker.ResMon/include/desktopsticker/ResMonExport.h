#pragma once

// 文件名刻意不叫 Export.h：EXE 同时包含多个 DLL 的头，相同相对路径会被
// #pragma once 跳过，导致后一个项目的宏永不定义（WallPaper 已踩过此坑）。
#ifdef DESKTOPSTICKER_RESMON_BUILD
#define DESKTOPSTICKER_RESMON_API __declspec(dllexport)
#else
#define DESKTOPSTICKER_RESMON_API __declspec(dllimport)
#endif
