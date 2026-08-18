#pragma once

#ifdef DESKTOPSTICKER_BUILD
#define DESKTOPSTICKER_API __declspec(dllexport)
#else
#define DESKTOPSTICKER_API __declspec(dllimport)
#endif
