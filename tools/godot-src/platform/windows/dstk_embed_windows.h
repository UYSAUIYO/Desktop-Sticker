/**************************************************************************/
/*  dstk_embed_windows.h                                                  */
/**************************************************************************/
/*  Desktop Sticker fork 专用：宿主嵌入层（外部 HWND 呈现）。               */
/*  不属于上游 Godot；由 .zcode/plans/ 的场景壁纸方案维护。                  */
/**************************************************************************/

#pragma once

#include <cstdint>
#include <windows.h>

#include "core/extension/libgodot.h"

// 非空 = 嵌入模式：引擎主窗口的呈现目标由宿主提供（桌面壁纸窗口）。
// 引擎不创建/不显示该窗口，也不改动它的样式、区域、IME 与属性。
HWND dstk_embed_present_hwnd();

// ---------------------------------------------------------------------------
// 鼠标事件注入（桌面壁纸交互）
//
// 嵌入模式下引擎**不子类化**宿主窗口：壁纸窗口在桌面图标层之下，
// 正常情况下永远收不到鼠标消息。宿主的做法是装一个低级鼠标钩子，
// 判断"点在空白桌面上"之后自己吞掉并转发到这里，由引擎构造 InputEventMouse*。
//
// 调用线程：必须是驱动 dstk_godot_iteration() 的那条线程（引擎主线程）。
// 参数一律用整数，避免宿主与运行库之间的浮点 ABI 差异。
// ---------------------------------------------------------------------------

enum DstkMouseInputType {
	DSTK_MOUSE_INPUT_MOVE = 0,   // 移动（relX/relY 为相对上一事件的像素位移）
	DSTK_MOUSE_INPUT_BUTTON = 1, // 按下/抬起；滚轮也用这里，button 为 WHEEL_*
};

// button 取 MouseButton 枚举值（1=左 2=右 3=中 4=滚轮上 5=滚轮下 6/7=水平滚轮）。
// buttonMask 取 MouseButtonMask 位掩码（当前按住的按钮）。
// mods：bit0=Ctrl bit1=Shift bit2=Alt bit3=Meta。
struct DstkMouseInput {
	int32_t type = DSTK_MOUSE_INPUT_MOVE;
	int32_t x = 0;          // 壁纸窗口客户区像素
	int32_t y = 0;
	int32_t relX = 0;
	int32_t relY = 0;
	int32_t button = 0;
	int32_t pressed = 0;
	int32_t wheelDelta = 0; // 滚轮增量（±120 的倍数）
	int32_t buttonMask = 0;
	int32_t mods = 0;
};

extern "C" LIBGODOT_API void dstk_embed_input_mouse(const DstkMouseInput *p_input);
