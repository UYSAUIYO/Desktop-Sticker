extends RefCounted
## manifest.json 组装。字段与 WallPaperPackage.h 的 WallPaperManifest 一一对应。
##
## dimension 固定写 "2d"；content_flags 目前只定义 "scripts"。

const Permissions = preload("res://src/export/permissions.gd")


static func build(doc, main_scene: String, content_flags: Array, script_dir: String) -> Dictionary:
	var vi := Engine.get_version_info()
	return {
		"format_version": 1,
		"title": doc.name,
		"author": "",
		"description": "",
		"pack_id": doc.pack_id,
		# dimension 是 C++ 端 WallPaperManifest 的既有字段："2d" = 控件场景，
		# "3d" = Node3D 场景（宿主据此判断要不要开 3D 渲染/相机）。
		"dimension": "3d" if doc.is_3d() else "2d",
		"tags": [],
		"categories": [],
		"main_scene": main_scene,
		"engine": {
			"version": "%d.%d.%d" % [vi.major, vi.minor, vi.patch],
			"build": vi.string,
		},
		"created_utc": Time.get_datetime_string_from_system(true, true),
		"requested_permissions": Permissions.derive(doc, script_dir),
		"content_flags": content_flags,
	}
