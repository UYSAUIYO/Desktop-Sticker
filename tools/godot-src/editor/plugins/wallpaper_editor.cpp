/**************************************************************************/
/*  wallpaper_editor.cpp  —  Desktop Sticker fork（P2）                    */
/**************************************************************************/

#include "wallpaper_editor.h"
#include "wallpaper_effect_gallery.h"
#include "wallpaper_effects_builtin.h"

#include "core/object/callable_mp.h"
#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "scene/2d/sprite_2d.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/item_list.h"
#include "scene/gui/label.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/subviewport_container.h"
#include "scene/main/canvas_item.h"
#include "scene/main/node.h"
#include "scene/main/viewport.h"
#include "scene/resources/material.h"
#include "scene/resources/shader.h"
#include "scene/resources/texture.h"

namespace {
const char *kMetaKey = "wp_effects";
const Vector2i kPreviewSize(480, 270);
} // namespace

void WallpaperEditor::_build_dock() {
	dock = memnew(MarginContainer);
	MarginContainer *mc = Object::cast_to<MarginContainer>(dock);
	mc->add_theme_constant_override("margin_left", 8);
	mc->add_theme_constant_override("margin_top", 6);
	mc->add_theme_constant_override("margin_right", 8);
	mc->add_theme_constant_override("margin_bottom", 6);

	HBoxContainer *root = memnew(HBoxContainer);
	root->add_theme_constant_override("separation", 12);
	dock->add_child(root);

	// 左：效果栈 + 工具条
	VBoxContainer *left = memnew(VBoxContainer);
	left->set_custom_minimum_size(Vector2(260, 0));
	left->add_theme_constant_override("separation", 6);
	root->add_child(left);

	target_label = memnew(Label);
	target_label->set_text(String::utf8("未选中节点"));
	left->add_child(target_label);

	HBoxContainer *tools = memnew(HBoxContainer);
	tools->add_theme_constant_override("separation", 6);
	left->add_child(tools);

	add_btn = memnew(Button);
	add_btn->set_text(String::utf8("添加效果…"));
	add_btn->connect(SNAME("pressed"), callable_mp(this, &WallpaperEditor::_on_add_pressed));
	tools->add_child(add_btn);

	Button *rm = memnew(Button);
	rm->set_text(String::utf8("删除"));
	rm->connect(SNAME("pressed"), callable_mp(this, &WallpaperEditor::_on_remove));
	tools->add_child(rm);

	Button *up = memnew(Button);
	up->set_text(String::utf8("上移"));
	up->connect(SNAME("pressed"), callable_mp(this, &WallpaperEditor::_on_move_up));
	tools->add_child(up);

	Button *down = memnew(Button);
	down->set_text(String::utf8("下移"));
	down->connect(SNAME("pressed"), callable_mp(this, &WallpaperEditor::_on_move_down));
	tools->add_child(down);

	Button *bake = memnew(Button);
	bake->set_text(String::utf8("烘焙到节点"));
	bake->set_tooltip_text(String::utf8("把栈顶效果作为材质写到节点本身（单层预览用）"));
	bake->connect(SNAME("pressed"), callable_mp(this, &WallpaperEditor::_on_bake));
	tools->add_child(bake);

	stack_list = memnew(ItemList);
	stack_list->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	stack_list->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	left->add_child(stack_list);

	// 右：SubViewport 链式合成预览
	{
		VBoxContainer *pv = memnew(VBoxContainer);
		root->add_child(pv);
		Label *pl = memnew(Label);
		pl->set_text(String::utf8("叠加预览"));
		pv->add_child(pl);
		preview = memnew(SubViewportContainer);
		preview->set_stretch(true);
		preview->set_custom_minimum_size(Vector2(kPreviewSize.x, kPreviewSize.y));
		preview->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		preview->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		pv->add_child(preview);
	}

	final_vp = memnew(SubViewport);
	final_vp->set_size(kPreviewSize);
	final_vp->set_transparent_background(true);
	final_vp->set_update_mode(SubViewport::UPDATE_ALWAYS);
	preview->add_child(final_vp);

	final_sprite = memnew(Sprite2D);
	final_sprite->set_centered(false);
	final_vp->add_child(final_sprite);

	// 各趟 SubViewport 的宿主（必须在树里才会渲染）
	passes = memnew(Node);
	dock->add_child(passes);

	// 「添加效果」画廊弹窗（常驻，按需弹出）
	gallery = memnew(WallpaperEffectGallery);
	gallery->set_visible(false);
	dock->add_child(gallery);
	gallery->connect(SNAME("confirmed"), callable_mp(this, &WallpaperEditor::_on_gallery_confirmed));
}

Ref<ShaderMaterial> WallpaperEditor::_make_material(int p_index) {
	ERR_FAIL_INDEX_V(p_index, kWpEffectCount, Ref<ShaderMaterial>());
	Ref<Shader> shader;
	shader.instantiate();
	shader->set_code(String::utf8(kWpEffects[p_index].code));
	Ref<ShaderMaterial> mat;
	mat.instantiate();
	mat->set_shader(shader);
	return mat;
}

void WallpaperEditor::_on_selection_changed() {
	_refresh_target();
}

void WallpaperEditor::_refresh_target() {
	EditorSelection *sel = get_editor_interface()->get_selection();
	current_target = nullptr;
	if (sel) {
		List<Node *> nodes = sel->get_full_selected_node_list();
		for (Node *n : nodes) {
			CanvasItem *ci = Object::cast_to<CanvasItem>(n);
			if (ci) {
				current_target = ci;
				break;
			}
		}
	}
	if (current_target) {
		target_label->set_text(String::utf8("目标：") + String(current_target->get_name()));
	} else {
		target_label->set_text(String::utf8("未选中节点"));
	}
	_refresh_list();
	_rebuild_preview();
}

PackedInt32Array WallpaperEditor::_read_effects() const {
	PackedInt32Array out;
	if (current_target && current_target->has_meta(String::utf8(kMetaKey))) {
		Variant v = current_target->get_meta(String::utf8(kMetaKey));
		if (v.get_type() == Variant::PACKED_INT32_ARRAY) {
			out = v;
		}
	}
	return out;
}

void WallpaperEditor::_write_effects(const PackedInt32Array &p_list) {
	if (current_target) {
		current_target->set_meta(String::utf8(kMetaKey), p_list);
	}
}

void WallpaperEditor::_refresh_list() {
	stack_list->clear();
	PackedInt32Array eff = _read_effects();
	for (int i = 0; i < eff.size(); i++) {
		int idx = eff[i];
		if (idx >= 0 && idx < kWpEffectCount) {
			stack_list->add_item(String::utf8(kWpEffects[idx].name));
		}
	}
}

void WallpaperEditor::_rebuild_preview() {
	// 清掉旧的各趟 SubViewport
	TypedArray<Node> kids = passes->get_children();
	for (int i = 0; i < kids.size(); i++) {
		Node *c = Object::cast_to<Node>(kids[i]);
		if (c) {
			memdelete(c);
		}
	}

	Ref<Texture2D> prev;
	Sprite2D *sp = current_target ? Object::cast_to<Sprite2D>(current_target) : nullptr;
	if (sp) {
		prev = sp->get_texture();
	}

	PackedInt32Array eff = _read_effects();
	for (int i = 0; i < eff.size(); i++) {
		int idx = eff[i];
		if (idx < 0 || idx >= kWpEffectCount) {
			continue;
		}
		SubViewport *vp = memnew(SubViewport);
		vp->set_size(kPreviewSize);
		vp->set_transparent_background(true);
		vp->set_update_mode(SubViewport::UPDATE_ALWAYS);
		passes->add_child(vp);

		Sprite2D *s = memnew(Sprite2D);
		s->set_centered(false);
		if (prev.is_valid()) {
			s->set_texture(prev);
		}
		s->set_material(_make_material(idx));
		vp->add_child(s);

		prev = vp->get_texture();
	}

	final_sprite->set_texture(prev);
}

void WallpaperEditor::_on_add_pressed() {
	if (!current_target || !gallery) {
		return;
	}
	gallery->popup_centered(Vector2i(720, 480));
}

void WallpaperEditor::_on_gallery_confirmed() {
	if (!current_target || !gallery) {
		return;
	}
	const int idx = gallery->get_selected();
	if (idx < 0) {
		return;
	}
	PackedInt32Array eff = _read_effects();
	eff.append(idx);
	_write_effects(eff);
	_refresh_list();
	_rebuild_preview();
}

void WallpaperEditor::_on_remove() {
	if (!current_target) {
		return;
	}
	PackedInt32Array sel = stack_list->get_selected_items();
	if (sel.is_empty()) {
		return;
	}
	PackedInt32Array eff = _read_effects();
	int at = sel[0];
	if (at >= 0 && at < eff.size()) {
		eff.remove_at(at);
		_write_effects(eff);
		_refresh_list();
		_rebuild_preview();
	}
}

void WallpaperEditor::_on_move_up() {
	if (!current_target) {
		return;
	}
	PackedInt32Array sel = stack_list->get_selected_items();
	if (sel.is_empty()) {
		return;
	}
	int at = sel[0];
	PackedInt32Array eff = _read_effects();
	if (at > 0 && at < eff.size()) {
		int tmp = eff[at];
		eff.set(at, eff[at - 1]);
		eff.set(at - 1, tmp);
		_write_effects(eff);
		_refresh_list();
		stack_list->select(at - 1);
		_rebuild_preview();
	}
}

void WallpaperEditor::_on_move_down() {
	if (!current_target) {
		return;
	}
	PackedInt32Array sel = stack_list->get_selected_items();
	if (sel.is_empty()) {
		return;
	}
	int at = sel[0];
	PackedInt32Array eff = _read_effects();
	if (at >= 0 && at < eff.size() - 1) {
		int tmp = eff[at];
		eff.set(at, eff[at + 1]);
		eff.set(at + 1, tmp);
		_write_effects(eff);
		_refresh_list();
		stack_list->select(at + 1);
		_rebuild_preview();
	}
}

void WallpaperEditor::_on_bake() {
	if (!current_target) {
		return;
	}
	PackedInt32Array eff = _read_effects();
	if (eff.is_empty()) {
		return;
	}
	current_target->set_material(_make_material(eff[eff.size() - 1]));
}

String WallpaperEditor::get_plugin_name() const {
	return "WallpaperEffects";
}

void WallpaperEditor::_bind_methods() {}

WallpaperEditor::WallpaperEditor() {
	_build_dock();
	add_control_to_bottom_panel(dock, String::utf8("壁纸效果库"));

	EditorSelection *sel = get_editor_interface()->get_selection();
	if (sel) {
		sel->connect(SNAME("selection_changed"), callable_mp(this, &WallpaperEditor::_on_selection_changed));
	}
}

WallpaperEditor::~WallpaperEditor() {
	EditorSelection *sel = get_editor_interface()->get_selection();
	if (sel) {
		sel->disconnect(SNAME("selection_changed"), callable_mp(this, &WallpaperEditor::_on_selection_changed));
	}
	if (dock) {
		remove_control_from_bottom_panel(dock);
		memdelete(dock);
		dock = nullptr;
	}
}
