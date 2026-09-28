/**************************************************************************/
/*  editor_wallpaper_plugin.h  —  Desktop Sticker fork                     */
/**************************************************************************/
/* 内置「壁纸」导出插件：不依赖任何工程内 addon，任何工程都能从 项目>工具 */
/* 菜单直接校验场景、导出 .dswall 壁纸包、并一键投递到桌面贴纸收件箱热应用。*/
/**************************************************************************/

#pragma once

#include "editor/plugins/editor_plugin.h"

class EditorWallpaperPlugin : public EditorPlugin {
	GDCLASS(EditorWallpaperPlugin, EditorPlugin);

	void _on_export(bool p_apply);
	void _on_validate();
	PackedStringArray _collect_problems() const;
	void _ensure_export_preset();
	String _manifest_json();
	String _pack_id();
	void _notify(const String &p_title, const String &p_text);

	static void _bind_methods();

public:
	EditorWallpaperPlugin();
};
