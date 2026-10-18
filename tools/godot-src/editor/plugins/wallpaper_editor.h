/**************************************************************************/
/*  wallpaper_editor.h  —  Desktop Sticker fork（P2）                      */
/*  一层多效果叠加：节点 meta 存有序效果栈，停靠面板用 SubViewport 链      */
/*  逐趟合成实时预览。复用 EditorPlugin + EditorSelection + Shader 系统。  */
/**************************************************************************/

#pragma once

#include "editor/plugins/editor_plugin.h"

class ItemList;
class Button;
class Label;
class SubViewportContainer;
class SubViewport;
class Sprite2D;
class Node;
class CanvasItem;
class ShaderMaterial;
class WallpaperEffectGallery;

class WallpaperEditor : public EditorPlugin {
	GDCLASS(WallpaperEditor, EditorPlugin);

	Control *dock = nullptr;
	ItemList *stack_list = nullptr;
	Button *add_btn = nullptr;
	Label *target_label = nullptr;
	WallpaperEffectGallery *gallery = nullptr;
	SubViewportContainer *preview = nullptr;
	Node *passes = nullptr;        // 承载各趟 SubViewport（在树里才会渲染）
	SubViewport *final_vp = nullptr;
	Sprite2D *final_sprite = nullptr;

	CanvasItem *current_target = nullptr;

	void _build_dock();
	void _on_selection_changed();
	void _refresh_target();
	PackedInt32Array _read_effects() const;
	void _write_effects(const PackedInt32Array &p_list);
	void _refresh_list();
	void _rebuild_preview();
	Ref<ShaderMaterial> _make_material(int p_index);

	void _on_add_pressed();
	void _on_gallery_confirmed();
	void _on_remove();
	void _on_move_up();
	void _on_move_down();
	void _on_bake();

protected:
	static void _bind_methods();

public:
	virtual String get_plugin_name() const override;

	WallpaperEditor();
	~WallpaperEditor();
};
