extends RefCounted
## 导出编排。**完全同步**，不弹 UI，内部不 await —— 海报由调用方（UI 或测试）先渲染好
## 再传进来。这样整条导出链路都能被无头测试直接调用。

const StudioPaths = preload("res://src/doc/paths.gd")
const SceneBuilder = preload("res://src/build/scene_builder.gd")
const Packer = preload("res://src/export/packer.gd")
const StudioManifest = preload("res://src/export/manifest.gd")


## 返回 {ok, error, problem_list, path}
static func export(doc, project_dir: String, out_path: String, poster_path: String) -> Dictionary:
	# 1) 校验（阻塞式：有问题就一个文件都不生成）
	var problems = doc.validate(project_dir)
	if not problems.is_empty():
		return {"ok": false, "error": "校验未通过", "problem_list": problems, "path": ""}

	var key := Packer.load_key()
	if key.is_empty():
		return {"ok": false, "error": "缺少或非法的 signing_key.txt（studio 目录下）",
				"problem_list": [], "path": ""}

	# 2) 构建目录：只放 res:// 内容（main.tscn + assets），整目录重建。
	#    它必须在 res:// 下，ResourceSaver 才会把路径本地化成 res:// 相对路径（设计 §2.2）。
	var build_res := StudioPaths.build_dir(doc.pack_id)
	var build_abs := ProjectSettings.globalize_path(build_res)
	_delete_dir_recursive(build_abs)
	if DirAccess.make_dir_recursive_absolute(build_abs) != OK:
		return {"ok": false, "error": "无法创建构建目录（res:// 不可写？Studio 必须以目录形式运行）：" + build_abs,
				"problem_list": [], "path": ""}
	var assets_res := build_res.path_join("assets")
	var assets_abs := ProjectSettings.globalize_path(assets_res)
	DirAccess.make_dir_recursive_absolute(assets_abs)

	# 中间产物（海报、PCK）放 scratch：放进构建目录的话会被 _walk 一起打进包里。
	var scratch_abs := ProjectSettings.globalize_path(StudioPaths.scratch_dir(doc.pack_id))
	DirAccess.make_dir_recursive_absolute(scratch_abs)

	# 3) 拷资源。构建目录里按文件名平铺 —— 用户工程里 src 可能是 assets/a.png 这种相对路径，
	#    平铺后不会出现 assets/assets/a.png 的嵌套。
	#    图层是树，所以遍历走 doc.asset_refs()（含容器里的子层与图片遮罩）。
	for ref in doc.asset_refs():
		var rel := String(ref["rel"])
		if rel.is_empty():
			continue
		if not _copy_file(project_dir.path_join(rel), assets_abs.path_join(rel.get_file())):
			return {"ok": false, "error": "复制资源失败：" + rel, "problem_list": [], "path": ""}
		# 同名不同目录的两个资源会互相覆盖：明确报出来，不做静默取舍。
		if not _same_file(project_dir.path_join(rel), assets_abs.path_join(rel.get_file())):
			return {"ok": false, "error": "资源文件名冲突：%s（不同目录下同名）" % rel,
					"problem_list": [], "path": ""}

	# 4) 生成场景并保存成**二进制** .scn。这一点是实测倒逼出来的：
	#
	#    template_release 运行库里 ResourceLoader **不认识裸 .png**（只有编辑器导入
	#    产出的 .ctex 能加载），所以贴图没办法做成 ext_resource 引用 —— 只能内联。
	#    而文本 .tscn 把内联字节写成十进制数字列表，一张 2 MB 的 PNG 会膨胀成 ~8 MB。
	#    二进制 .scn 里就是原始字节，膨胀约 1x，而且加载不用解析文本。
	#
	#    代价：场景不可读。但它本来就要被 AES 加密进 pck，可读性没有价值。
	var root = SceneBuilder.build(doc, assets_res)
	var scene := PackedScene.new()
	if scene.pack(root) != OK:
		root.free()
		return {"ok": false, "error": "PackedScene.pack 失败", "problem_list": [], "path": ""}
	root.free()
	var main_scene_res := build_res.path_join("main.scn")
	var save_err := ResourceSaver.save(scene, main_scene_res)
	if save_err != OK:
		return {"ok": false,
				"error": "保存场景失败（err=%d）。res:// 是否可写？Studio 必须以目录形式运行。" % save_err,
				"problem_list": [], "path": ""}

	# 5) 加密 PCK。res_prefix 保证包内路径与 main.tscn 里的引用一致。
	var tmp_pck := scratch_abs.path_join("project.pck")
	if FileAccess.file_exists(tmp_pck):
		DirAccess.remove_absolute(tmp_pck)
	var pack_err := Packer.pack_encrypted(build_abs, build_res.trim_prefix("res://"), tmp_pck, key)
	if pack_err != OK:
		return {"ok": false, "error": "打包 PCK 失败（err=%d）" % pack_err, "problem_list": [], "path": ""}

	# 6) manifest + .dswall
	var content_flags := []
	if not doc.external_scripts().is_empty():
		content_flags.append("scripts")
	var manifest_text := JSON.stringify(
			StudioManifest.build(doc, main_scene_res, content_flags, project_dir), "  ")
	var zip_err := Packer.assemble_dswall(out_path, tmp_pck, manifest_text, poster_path)
	if zip_err != OK:
		return {"ok": false, "error": "写入 .dswall 失败（err=%d）" % zip_err, "problem_list": [], "path": ""}

	return {"ok": true, "error": "", "problem_list": [], "path": out_path}


## 投递到桌面贴纸收件箱（热应用）。先写 .tmp 再改名，收件箱扫描器只认 .dswall。
static func deliver_to_inbox(dswall_path: String) -> Dictionary:
	var inbox := StudioPaths.inbox_dir()
	if inbox.is_empty():
		return {"ok": false, "error": "读不到 %APPDATA%"}
	if DirAccess.make_dir_recursive_absolute(inbox) != OK:
		return {"ok": false, "error": "收件箱不可写：" + inbox}
	var final := inbox.path_join(dswall_path.get_file())
	var tmp := final + ".tmp"
	var bytes := FileAccess.get_file_as_bytes(dswall_path)
	if bytes.is_empty():
		return {"ok": false, "error": "读不到导出文件：" + dswall_path}
	var f := FileAccess.open(tmp, FileAccess.WRITE)
	if f == null:
		return {"ok": false, "error": "收件箱不可写：" + tmp}
	f.store_buffer(bytes)
	f.close()
	if FileAccess.file_exists(final):
		DirAccess.remove_absolute(final)
	if DirAccess.rename_absolute(tmp, final) != OK:
		return {"ok": false, "error": "投递失败：" + final}
	return {"ok": true, "error": "", "path": final}


static func _copy_file(src: String, dst: String) -> bool:
	var bytes := FileAccess.get_file_as_bytes(src)
	if bytes.is_empty():
		return false
	var f := FileAccess.open(dst, FileAccess.WRITE)
	if f == null:
		return false
	f.store_buffer(bytes)
	f.close()
	return true


static func _same_file(a: String, b: String) -> bool:
	return FileAccess.get_file_as_bytes(a) == FileAccess.get_file_as_bytes(b)


static func _delete_dir_recursive(abs_path: String) -> void:
	if not DirAccess.dir_exists_absolute(abs_path):
		return
	var da := DirAccess.open(abs_path)
	if da == null:
		return
	for f in da.get_files():
		DirAccess.remove_absolute(abs_path.path_join(f))
	for d in da.get_directories():
		_delete_dir_recursive(abs_path.path_join(d))
	DirAccess.remove_absolute(abs_path)
