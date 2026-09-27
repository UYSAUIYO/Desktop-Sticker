/**************************************************************************/
/*  dstk_embed_windows.cpp                                                */
/**************************************************************************/
/*  Desktop Sticker fork 专用：宿主嵌入层。                                  */
/*                                                                        */
/*  宿主（桌面贴纸壁纸模块）在创建 Godot 实例前调用                        */
/*  dstk_embed_set_present_hwnd()，把桌面壁纸窗口交给引擎做呈现目标；       */
/*  display_server_windows.cpp 在该句柄有效时走"嵌入分支"。                 */
/**************************************************************************/

#include "dstk_embed_windows.h"

#include "core/extension/libgodot.h"

static HWND g_dstk_present_hwnd = nullptr;

HWND dstk_embed_present_hwnd() {
	return g_dstk_present_hwnd;
}

extern "C" {

LIBGODOT_API void dstk_embed_set_present_hwnd(void *p_hwnd) {
	g_dstk_present_hwnd = (HWND)p_hwnd;
}

LIBGODOT_API void *dstk_embed_get_present_hwnd() {
	return g_dstk_present_hwnd;
}

} // extern "C"
