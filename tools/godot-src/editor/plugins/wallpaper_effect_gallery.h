/**************************************************************************/
/*  wallpaper_effect_gallery.h  —  Desktop Sticker fork（P3）              */
/*  「添加效果」画廊弹窗：左侧分类 + 搜索框 + 效果按钮网格。                */
/*  参照 Wallpaper Engine 的 Add Effect 对话框；复用 AcceptDialog/ItemList */
/**************************************************************************/

#pragma once

#include "scene/gui/dialogs.h"

class ItemList;
class GridContainer;
class LineEdit;
class Label;

class WallpaperEffectGallery : public AcceptDialog {
	GDCLASS(WallpaperEffectGallery, AcceptDialog);

	ItemList *categories = nullptr;
	LineEdit *search = nullptr;
	GridContainer *grid = nullptr;
	Label *sel_label = nullptr;

	int selected = -1;

	void _rebuild_grid();
	void _on_category_selected(int p_index);
	void _on_search_changed(const String &p_text);
	void _on_pick(int p_index);

protected:
	static void _bind_methods();

public:
	int get_selected() const { return selected; }

	WallpaperEffectGallery();
};
