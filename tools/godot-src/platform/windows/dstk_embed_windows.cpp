/**************************************************************************/
/*  dstk_embed_windows.cpp                                                */
/**************************************************************************/
/*  Desktop Sticker fork 专用：宿主嵌入层。                                  */
/*                                                                        */
/*  宿主（桌面贴纸壁纸模块）在创建 Godot 实例前调用                        */
/*  dstk_embed_set_present_hwnd()，把桌面壁纸窗口交给引擎做呈现目标；       */
/*  display_server_windows.cpp 在该句柄有效时走"嵌入分支"。                 */
/*                                                                        */
/*  嵌入分支只借用窗口的呈现能力，不接管消息（引擎不会子类化它），         */
/*  因此鼠标事件由宿主的低级钩子捕获后经 dstk_embed_input_mouse() 注入。     */
/**************************************************************************/

#include "dstk_embed_windows.h"

#include "core/extension/libgodot.h"
#include "core/input/input.h"
#include "core/input/input_enums.h"
#include "core/input/input_event.h"
#include "servers/display/display_server_enums.h"

static HWND g_dstk_present_hwnd = nullptr;

HWND dstk_embed_present_hwnd() {
	return g_dstk_present_hwnd;
}

static void dstk_apply_mods(Ref<InputEventWithModifiers> p_event, int32_t p_mods) {
	p_event->set_ctrl_pressed((p_mods & 1) != 0);
	p_event->set_shift_pressed((p_mods & 2) != 0);
	p_event->set_alt_pressed((p_mods & 4) != 0);
	p_event->set_meta_pressed((p_mods & 8) != 0);
}

extern "C" {

LIBGODOT_API void dstk_embed_set_present_hwnd(void *p_hwnd) {
	g_dstk_present_hwnd = (HWND)p_hwnd;
}

LIBGODOT_API void *dstk_embed_get_present_hwnd() {
	return g_dstk_present_hwnd;
}

LIBGODOT_API void dstk_embed_input_mouse(const DstkMouseInput *p_input) {
	Input *input = Input::get_singleton();
	if (p_input == nullptr || input == nullptr) {
		return;
	}

	const Vector2 position((real_t)p_input->x, (real_t)p_input->y);
	const BitField<MouseButtonMask> mask = MouseButtonMask(p_input->buttonMask & 0x0F);

	if (p_input->type == DSTK_MOUSE_INPUT_MOVE) {
		Ref<InputEventMouseMotion> mm;
		mm.instantiate();
		mm->set_window_id(DisplayServerEnums::MAIN_WINDOW_ID);
		mm->set_position(position);
		mm->set_global_position(position);
		mm->set_button_mask(mask);
		dstk_apply_mods(mm, p_input->mods);

		const Vector2 relative((real_t)p_input->relX, (real_t)p_input->relY);
		mm->set_relative(relative);
		mm->set_relative_screen_position(relative);
		mm->set_velocity(relative);
		mm->set_screen_velocity(relative);
		input->parse_input_event(mm);
		return;
	}

	Ref<InputEventMouseButton> mb;
	mb.instantiate();
	mb->set_window_id(DisplayServerEnums::MAIN_WINDOW_ID);
	mb->set_position(position);
	mb->set_global_position(position);
	mb->set_button_index(MouseButton(p_input->button));
	mb->set_pressed(p_input->pressed != 0);
	mb->set_button_mask(mask);
	dstk_apply_mods(mb, p_input->mods);

	const bool wheel = p_input->button >= (int32_t)MouseButton::WHEEL_UP &&
			p_input->button <= (int32_t)MouseButton::WHEEL_RIGHT;
	if (wheel) {
		const int32_t delta = p_input->wheelDelta != 0 ? p_input->wheelDelta : 120;
		const real_t factor = MAX(0.25f, Math::abs((real_t)delta) / 120.0f);
		mb->set_factor(factor);
	}

	input->parse_input_event(mb);

	// 滚轮与 DisplayServerWindows 的处理保持一致：按下之后再补一个抬起，
	// 否则 UI 里"一直按着滚轮"的状态会留着。
	if (wheel && mb->is_pressed()) {
		Ref<InputEventMouseButton> release = mb->duplicate();
		release->set_pressed(false);
		input->parse_input_event(release);
	}
}

} // extern "C"
