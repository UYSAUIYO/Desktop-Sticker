/**************************************************************************/
/*  dstk_embed_windows.cpp                                                */
/**************************************************************************/
/*  Desktop Sticker fork 专用：宿主嵌入层。                                  */
/*                                                                        */
/*  宿主（桌面贴纸壁纸模块）在创建 Godot 实例前调用                        */
/*  dstk_embed_set_present_hwnd()，把桌面壁纸窗口交给引擎做呈现目标；       */
/*  display_server_windows.cpp 在该句柄有效时走"嵌入分支"。                 */
/*                                                                        */
/*  嵌入分支只借用窗口的呈现能力，默认不接管消息；交互壁纸需要鼠标，         */
/*  所以这里给该窗口装一层薄壳窗口过程，把**鼠标消息**交给引擎自己的         */
/*  DisplayServerWindows::WndProc —— 和真实窗口完全同一条处理路径。         */
/**************************************************************************/

#include "dstk_embed_windows.h"

#include "core/extension/libgodot.h"
#include "display_server_windows.h"

static HWND g_dstk_present_hwnd = nullptr;
static WNDPROC g_dstk_prev_proc = nullptr;

HWND dstk_embed_present_hwnd() {
	return g_dstk_present_hwnd;
}

static bool is_mouse_message(UINT p_msg) {
	switch (p_msg) {
		case WM_MOUSEMOVE:
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_LBUTTONDBLCLK:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
		case WM_RBUTTONDBLCLK:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
		case WM_MBUTTONDBLCLK:
		case WM_XBUTTONDOWN:
		case WM_XBUTTONUP:
		case WM_XBUTTONDBLCLK:
		case WM_MOUSEWHEEL:
		case WM_MOUSEHWHEEL:
		case WM_MOUSELEAVE:
			return true;
		default:
			return false;
	}
}

// 薄壳过程：鼠标消息走引擎原生处理，其余消息回交给宿主原来的窗口过程
// （宿主自己的 WM_NCHITTEST=HTTRANSPARENT / WM_MOUSEACTIVATE 等语义一字不改）。
static LRESULT CALLBACK dstk_input_wnd_proc(HWND p_hwnd, UINT p_msg, WPARAM p_wparam, LPARAM p_lparam) {
	if (is_mouse_message(p_msg)) {
		DisplayServerWindows *ds = Object::cast_to<DisplayServerWindows>(DisplayServer::get_singleton());
		if (ds) {
			return ds->WndProc(p_hwnd, p_msg, p_wparam, p_lparam);
		}
	}
	if (g_dstk_prev_proc) {
		return CallWindowProcW(g_dstk_prev_proc, p_hwnd, p_msg, p_wparam, p_lparam);
	}
	return DefWindowProcW(p_hwnd, p_msg, p_wparam, p_lparam);
}

void dstk_embed_install_input_proc() {
	if (!g_dstk_present_hwnd || g_dstk_prev_proc) {
		return;
	}
	g_dstk_prev_proc = (WNDPROC)SetWindowLongPtrW(g_dstk_present_hwnd, GWLP_WNDPROC, (LONG_PTR)dstk_input_wnd_proc);
}

extern "C" {

LIBGODOT_API void dstk_embed_set_present_hwnd(void *p_hwnd) {
	g_dstk_present_hwnd = (HWND)p_hwnd;
}

LIBGODOT_API void *dstk_embed_get_present_hwnd() {
	return g_dstk_present_hwnd;
}

} // extern "C"
