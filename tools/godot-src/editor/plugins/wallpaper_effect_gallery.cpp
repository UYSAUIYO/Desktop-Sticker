/**************************************************************************/
/*  wallpaper_effect_gallery.cpp  —  Desktop Sticker fork（P3）            */
/**************************************************************************/

#include "wallpaper_effect_gallery.h"
#include "wallpaper_effects_builtin.h"

#include "core/object/callable_mp.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/item_list.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/scroll_container.h"

WallpaperEffectGallery::WallpaperEffectGallery() {
	set_title(String::utf8("添加效果"));
	set_ok_button_text(String::utf8("确认"));

	MarginContainer *mc = memnew(MarginContainer);
	mc->add_theme_constant_override("margin_left", 10);
	mc->add_theme_constant_override("margin_top", 10);
	mc->add_theme_constant_override("margin_right", 10);
	mc->add_theme_constant_override("margin_bottom", 10);
	mc->set_custom_minimum_size(Vector2(680, 420));
	add_child(mc);

	HBoxContainer *root = memnew(HBoxContainer);
	root->add_theme_constant_override("separation", 12);
	mc->add_child(root);

	// 左：分类
	VBoxContainer *left = memnew(VBoxContainer);
	left->set_custom_minimum_size(Vector2(150, 0));
	left->add_theme_constant_override("separation", 6);
	root->add_child(left);

	Label *clabel = memnew(Label);
	clabel->set_text(String::utf8("分类"));
	left->add_child(clabel);

	categories = memnew(ItemList);
	categories->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	left->add_child(categories);
	categories->add_item(String::utf8("全部"));
	{
		// 从注册表里收集去重分类
		for (int i = 0; i < kWpEffectCount; i++) {
			const String cat = String::utf8(kWpEffects[i].category);
			bool seen = false;
			for (int j = 0; j < categories->get_item_count(); j++) {
				if (categories->get_item_text(j) == cat) {
					seen = true;
					break;
				}
			}
			if (!seen) {
				categories->add_item(cat);
			}
		}
	}
	categories->select(0);
	categories->connect(SNAME("item_selected"), callable_mp(this, &WallpaperEffectGallery::_on_category_selected));

	// 右：搜索 + 效果网格
	VBoxContainer *right = memnew(VBoxContainer);
	right->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	right->add_theme_constant_override("separation", 6);
	root->add_child(right);

	search = memnew(LineEdit);
	search->set_placeholder(String::utf8("搜索效果…"));
	right->add_child(search);
	search->connect(SNAME("text_changed"), callable_mp(this, &WallpaperEffectGallery::_on_search_changed));

	ScrollContainer *scroll = memnew(ScrollContainer);
	scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	right->add_child(scroll);

	grid = memnew(GridContainer);
	grid->set_columns(2);
	grid->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	scroll->add_child(grid);

	sel_label = memnew(Label);
	sel_label->set_text(String::utf8("未选择"));
	right->add_child(sel_label);

	_rebuild_grid();
}

void WallpaperEffectGallery::_rebuild_grid() {
	TypedArray<Node> kids = grid->get_children();
	for (int i = 0; i < kids.size(); i++) {
		Node *c = Object::cast_to<Node>(kids[i]);
		if (c) {
			memdelete(c);
		}
	}

	const String all = String::utf8("全部");
	String cat = all;
	PackedInt32Array selc = categories->get_selected_items();
	if (!selc.is_empty()) {
		cat = categories->get_item_text(selc[0]);
	}
	const String q = search ? search->get_text() : String();

	Ref<ButtonGroup> bg;
	bg.instantiate();

	for (int i = 0; i < kWpEffectCount; i++) {
		const String name = String::utf8(kWpEffects[i].name);
		const String ecat = String::utf8(kWpEffects[i].category);
		if (cat != all && ecat != cat) {
			continue;
		}
		if (!q.is_empty() && name.findn(q) == -1) {
			continue;
		}
		Button *b = memnew(Button);
		b->set_text(name);
		b->set_toggle_mode(true);
		b->set_button_group(bg);
		if (i == selected) {
			b->set_pressed(true);
		}
		b->connect(SNAME("pressed"), callable_mp(this, &WallpaperEffectGallery::_on_pick).bind(i));
		grid->add_child(b);
	}
}

void WallpaperEffectGallery::_on_category_selected(int p_index) {
	_rebuild_grid();
}

void WallpaperEffectGallery::_on_search_changed(const String &p_text) {
	_rebuild_grid();
}

void WallpaperEffectGallery::_on_pick(int p_index) {
	selected = p_index;
	if (sel_label) {
		sel_label->set_text(String::utf8("已选：") + String::utf8(kWpEffects[p_index].name));
	}
}

void WallpaperEffectGallery::_bind_methods() {}
