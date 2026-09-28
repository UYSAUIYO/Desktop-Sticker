/**************************************************************************/
/*  editor_wallpaper_plugin.cpp  —  Desktop Sticker fork                   */
/**************************************************************************/

#include "editor_wallpaper_plugin.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/math/math_funcs.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/templates/list.h"
#include "editor/editor_interface.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/gui/dialogs.h"
#include "scene/main/canvas_item.h"
#include "scene/main/node.h"
#include "scene/resources/packed_scene.h"

namespace {

const char *kPresetName = "WallpaperPack";

// 只转义 JSON 里的反斜杠/引号/控制字符；UTF-8 字节原样透传（JSON 允许裸 UTF-8）。
String json_escape(const String &p_in) {
	String out;
	for (int i = 0; i < p_in.length(); i++) {
		char32_t c = p_in[i];
		switch (c) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if (c < 0x20) {
					out += vformat("\\u%04x", (uint32_t)c);
				} else {
					out += (char32_t)c;
				}
		}
	}
	return out;
}

String quoted_array(const PackedStringArray &p_arr) {
	String s = "[";
	for (int i = 0; i < p_arr.size(); i++) {
		if (i > 0) {
			s += ", ";
		}
		s += "\"" + json_escape(p_arr[i]) + "\"";
	}
	s += "]";
	return s;
}

String get_str(const String &p_key, const String &p_def) {
	if (ProjectSettings::get_singleton()->has_setting(p_key)) {
		return GLOBAL_GET(p_key);
	}
	return p_def;
}

PackedStringArray get_arr(const String &p_key) {
	PackedStringArray out;
	if (ProjectSettings::get_singleton()->has_setting(p_key)) {
		const Variant v = GLOBAL_GET(p_key);
		if (v.get_type() == Variant::PACKED_STRING_ARRAY) {
			out = v;
		}
	}
	return out;
}

void scan_node(Node *p_node, int &r_cameras, bool &r_has3d, bool &r_has2d) {
	if (Object::cast_to<Camera3D>(p_node)) {
		r_cameras++;
	}
	if (Object::cast_to<Node3D>(p_node)) {
		r_has3d = true;
	}
	if (Object::cast_to<CanvasItem>(p_node)) {
		r_has2d = true;
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		scan_node(p_node->get_child(i), r_cameras, r_has3d, r_has2d);
	}
}

} // namespace

PackedStringArray EditorWallpaperPlugin::_collect_problems() const {
	PackedStringArray problems;
	const String scene = GLOBAL_GET("application/run/main_scene");
	if (scene.is_empty()) {
		problems.push_back(String::utf8("未设置主场景（项目设置 → 应用 → 运行 → 主场景）。"));
		return problems;
	}
	if (!ResourceLoader::exists(scene)) {
		problems.push_back(vformat(String::utf8("主场景不存在：%s"), scene));
		return problems;
	}
	Ref<PackedScene> ps = ResourceLoader::load(scene);
	if (ps.is_null()) {
		problems.push_back(vformat(String::utf8("主场景无法加载：%s"), scene));
		return problems;
	}
	Node *root = ps->instantiate();
	int cameras = 0;
	bool has3d = false;
	bool has2d = false;
	scan_node(root, cameras, has3d, has2d);
	memdelete(root);

	if (has3d) {
		if (cameras == 0) {
			problems.push_back(String::utf8("3D 场景里没有 Camera3D：壁纸会看不到画面。"));
		}
	} else if (!has2d) {
		problems.push_back(String::utf8("场景里既没有 3D 也没有 2D 可见内容，壁纸会是空屏。"));
	}
	return problems;
}

String EditorWallpaperPlugin::_pack_id() {
	String pid = get_str("desktop_sticker/wallpaper/pack_id", "");
	if (pid.is_empty()) {
		pid = vformat("dswall-%08x%08x",
				(uint32_t)(Math::rand() * 4294967295.0),
				(uint32_t)(Time::get_singleton()->get_ticks_usec()));
		ProjectSettings::get_singleton()->set_setting("desktop_sticker/wallpaper/pack_id", pid);
		ProjectSettings::get_singleton()->save();
	}
	return pid;
}

String EditorWallpaperPlugin::_manifest_json() {
	const String title = get_str("application/config/name", "wallpaper");
	const String author = get_str("desktop_sticker/wallpaper/author", "");
	const String description = get_str("desktop_sticker/wallpaper/description", "");
	const String dimension = get_str("desktop_sticker/wallpaper/dimension", "3d");
	const PackedStringArray tags = get_arr("desktop_sticker/wallpaper/tags");
	const PackedStringArray categories = get_arr("desktop_sticker/wallpaper/categories");
	const PackedStringArray permissions = get_arr("desktop_sticker/wallpaper/requested_permissions");
	const String pack_id = _pack_id();
	const String main_scene = GLOBAL_GET("application/run/main_scene");

	String m;
	m += "{\n";
	m += "  \"format_version\": 1,\n";
	m += "  \"title\": \"" + json_escape(title) + "\",\n";
	m += "  \"author\": \"" + json_escape(author) + "\",\n";
	m += "  \"description\": \"" + json_escape(description) + "\",\n";
	m += "  \"pack_id\": \"" + json_escape(pack_id) + "\",\n";
	m += "  \"dimension\": \"" + json_escape(dimension) + "\",\n";
	m += "  \"tags\": " + quoted_array(tags) + ",\n";
	m += "  \"categories\": " + quoted_array(categories) + ",\n";
	m += "  \"main_scene\": \"" + json_escape(main_scene) + "\",\n";
	m += "  \"created_utc\": \"" + Time::get_singleton()->get_datetime_string_from_system(true, true) + "\",\n";
	m += "  \"requested_permissions\": " + quoted_array(permissions) + "\n";
	m += "}\n";
	return m;
}

void EditorWallpaperPlugin::_ensure_export_preset() {
	const String cfg_path = ProjectSettings::get_singleton()->globalize_path("res://export_presets.cfg");
	String existing;
	bool has_preset = false;
	Ref<FileAccess> rf = FileAccess::open(cfg_path, FileAccess::READ);
	if (rf.is_valid()) {
		existing = rf->get_as_text();
		rf.unref();
		has_preset = existing.contains(vformat("name=\"%s\"", String(kPresetName)));
	}
	if (has_preset) {
		return;
	}

	int idx = 0;
	while (existing.contains(vformat("[preset.%d]", idx))) {
		idx++;
	}
	String block = vformat(
			"\n[preset.%d]\n\n"
			"name=\"%s\"\nplatform=\"Windows Desktop\"\nrunnable=true\nadvanced_options=false\n"
			"dedicated_server=false\ncustom_features=\"\"\nexport_filter=\"all_resources\"\n"
			"include_filter=\"\"\nexclude_filter=\"\"\nexport_path=\"\"\npatches=PackedStringArray()\n"
			"encryption_include_filters=\"\"\nencryption_exclude_filters=\"\"\nseed=0\n"
			"encrypt_pck=false\nencrypt_directory=false\nscript_export_mode=2\n\n",
			idx, String(kPresetName));

	Ref<FileAccess> wf = FileAccess::open(cfg_path, FileAccess::WRITE);
	if (wf.is_valid()) {
		wf->store_string(existing + block);
	}
}

void EditorWallpaperPlugin::_notify(const String &p_title, const String &p_text) {
	AcceptDialog *dlg = memnew(AcceptDialog);
	dlg->set_title(p_title);
	dlg->set_text(p_text);
	dlg->set_ok_button_text(String::utf8("确定"));
	dlg->connect(SNAME("confirmed"), Callable(dlg, SNAME("queue_free")));
	get_editor_interface()->get_base_control()->add_child(dlg);
	dlg->popup_centered();
}

void EditorWallpaperPlugin::_on_validate() {
	const PackedStringArray problems = _collect_problems();
	if (problems.is_empty()) {
		_notify(String::utf8("校验通过"), String::utf8("场景可以导出为壁纸包。"));
		return;
	}
	String t;
	for (const String &p : problems) {
		t += "- " + p + "\n";
	}
	_notify(String::utf8("校验未通过"), t);
}

void EditorWallpaperPlugin::_on_export(bool p_apply) {
	const PackedStringArray problems = _collect_problems();
	if (!problems.is_empty()) {
		String t;
		for (const String &p : problems) {
			t += "- " + p + "\n";
		}
		_notify(String::utf8("校验未通过"), t);
		return;
	}

	_ensure_export_preset();

	const String proj_dir = ProjectSettings::get_singleton()->globalize_path("res://");
	const String tmp = OS::get_singleton()->get_temp_path().path_join("dstk_wallpaper_export");
	DirAccess::make_dir_recursive_absolute(tmp);
	const String pck = tmp.path_join("project.pck");
	const String manifest = tmp.path_join("manifest.json");
	const String outzip = tmp.path_join("out.zip");

	Ref<DirAccess> fs = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	for (const String &f : { pck, manifest, outzip }) {
		if (FileAccess::exists(f)) {
			fs->remove(f);
		}
	}

	// 1) 用编辑器自身 CLI 导出 PCK（--export-pack 不需要平台导出模板）。
	{
		List<String> args;
		args.push_back("--headless");
		args.push_back("--path");
		args.push_back(proj_dir);
		args.push_back("--export-pack");
		args.push_back(String(kPresetName));
		args.push_back(pck);
		String output;
		int exitcode = 1;
		const Error e = OS::get_singleton()->execute(OS::get_singleton()->get_executable_path(), args, &output, &exitcode, true);
		if (e != OK || exitcode != 0 || !FileAccess::exists(pck)) {
			_notify(String::utf8("导出失败"), vformat(String::utf8("导出 PCK 失败（exit %d），详见编辑器输出。"), exitcode));
			return;
		}
	}

	// 2) manifest.json。
	{
		Ref<FileAccess> mf = FileAccess::open(manifest, FileAccess::WRITE);
		if (mf.is_null()) {
			_notify(String::utf8("导出失败"), String::utf8("无法写入 manifest.json。"));
			return;
		}
		mf->store_string(_manifest_json());
		mf.unref();
	}

	// 3) 用系统 tar.exe 打成 zip（-a 按 .zip 扩展名自动选格式），再改名为 .dswall。
	{
		List<String> targs;
		targs.push_back("-a");
		targs.push_back("-cf");
		targs.push_back(outzip);
		targs.push_back("-C");
		targs.push_back(tmp);
		targs.push_back("project.pck");
		targs.push_back("manifest.json");
		String tout;
		int tcode = 1;
		const Error te = OS::get_singleton()->execute("tar", targs, &tout, &tcode, true);
		if (te != OK || tcode != 0 || !FileAccess::exists(outzip)) {
			_notify(String::utf8("导出失败"), String::utf8("打包 .dswall 失败（tar 不可用或出错），详见编辑器输出。"));
			return;
		}
	}

	const String file_base = proj_dir.trim_suffix("\\").trim_suffix("/").get_file();
	const String dswall = proj_dir.path_join(file_base + ".dswall");
	if (FileAccess::exists(dswall)) {
		fs->remove(dswall);
	}
	if (fs->copy(outzip, dswall) != OK) {
		_notify(String::utf8("导出失败"), vformat(String::utf8("无法写出：%s"), dswall));
		return;
	}
	fs->remove(outzip);

	if (p_apply) {
		const String appdata = OS::get_singleton()->get_environment("APPDATA");
		if (!appdata.is_empty()) {
			const String inbox = appdata.path_join("DesktopSticker").path_join("wallpaper_inbox");
			DirAccess::make_dir_recursive_absolute(inbox);
			const String fin = inbox.path_join(file_base + ".dswall");
			const String tmp2 = inbox.path_join(file_base + ".dswall.tmp");
			if (FileAccess::exists(tmp2)) {
				fs->remove(tmp2);
			}
			if (fs->copy(dswall, tmp2) == OK) {
				if (FileAccess::exists(fin)) {
					fs->remove(fin);
				}
				fs->rename(tmp2, fin); // 同目录原子改名，模块只认 .dswall
			}
		}
		_notify(String::utf8("已导出并投递"), vformat(String::utf8("已导出并投递到桌面贴纸收件箱，稍候即会换上：\n%s"), dswall));
	} else {
		_notify(String::utf8("导出完成"), vformat(String::utf8("已导出壁纸包：\n%s"), dswall));
	}
}

void EditorWallpaperPlugin::_bind_methods() {}

EditorWallpaperPlugin::EditorWallpaperPlugin() {
	// Built-in editor plugins live for the editor's whole lifetime, so registering the
	// tool-menu entries in the constructor is enough (no _enter_tree/_exit_tree override:
	// those are GDVIRTUAL0 script hooks in Godot 4 and can't be C++-overridden).
	add_tool_menu_item(String::utf8("校验壁纸场景"), callable_mp(this, &EditorWallpaperPlugin::_on_validate));
	add_tool_menu_item(String::utf8("导出壁纸包 (.dswall)…"), callable_mp(this, &EditorWallpaperPlugin::_on_export).bind(false));
	add_tool_menu_item(String::utf8("导出并应用到桌面"), callable_mp(this, &EditorWallpaperPlugin::_on_export).bind(true));
}
