extends RefCounted
## assets 导入的用例。
##
## 重点不是"能拷文件"，而是两条容易悄悄坏掉的规则：
##   1. 重复选同一张图不产生副本（否则 assets/ 会迅速变脏）；
##   2. 同名但内容不同的图绝不覆盖（覆盖会让老图层在无提示的情况下换了图）。

const AssetImport = preload("res://src/ui/asset_import.gd")


func register(t) -> void:
	t.case("asset_import_copies_into_project", func() -> void:
		var root := _fresh_root("import_basic")
		var src := _write(root, "src/a.png", "AAA")
		var rel := AssetImport.import_image(root, src)
		t.eq(rel, "assets/a.png", "relative path")
		t.ok(FileAccess.file_exists(root.path_join(rel)), "file copied")
		_clean(root)
	)

	t.case("asset_import_reuses_identical_bytes", func() -> void:
		var root := _fresh_root("import_dedupe")
		var src := _write(root, "src/a.png", "AAA")
		var first := AssetImport.import_image(root, src)
		var second := AssetImport.import_image(root, src)
		t.eq(second, first, "same bytes reuse the same file")
		t.eq(DirAccess.get_files_at(root.path_join("assets")).size(), 1, "no duplicate copy")
		_clean(root)
	)

	t.case("asset_import_never_overwrites_different_bytes", func() -> void:
		var root := _fresh_root("import_collision")
		var a := _write(root, "src/a.png", "AAA")
		var b := _write(root, "other/a.png", "BBB")
		var first := AssetImport.import_image(root, a)
		var second := AssetImport.import_image(root, b)
		t.eq(first, "assets/a.png", "first copy keeps the name")
		t.eq(second, "assets/a_1.png", "collision gets a suffix")
		t.eq(FileAccess.get_file_as_string(root.path_join(first)), "AAA", "first file untouched")
		t.eq(FileAccess.get_file_as_string(root.path_join(second)), "BBB", "second file has new bytes")
		_clean(root)
	)

	t.case("asset_import_rejects_empty_inputs", func() -> void:
		t.eq(AssetImport.import_image("", "x.png"), "", "empty project dir")
		t.eq(AssetImport.import_image("user://somewhere", ""), "", "empty source path")
	)


	t.case("asset_import_frames_sorted_and_scoped", func() -> void:
		# 帧序列单独放一个目录：不同图层里同名的 frame_0001.png 不能互相覆盖。
		var root := _fresh_root("import_frames")
		var src := root.path_join("src_frames")
		DirAccess.make_dir_recursive_absolute(src)
		_write(root, "src_frames/b.png", "B")
		_write(root, "src_frames/a.png", "A")
		_write(root, "src_frames/note.txt", "x")
		var frames := AssetImport.import_frames(root, src, "petal")
		t.eq(frames.size(), 2, "只收图片，忽略其它文件")
		t.eq(String(frames[0]), "assets/petal_frames/petal_frames_0001.png", "按文件名排序 + 加前缀")
		t.eq(FileAccess.get_file_as_string(root.path_join(String(frames[1]))), "B", "内容拷过去")

		var again := AssetImport.import_frames(root, src, "petal")
		t.eq(String(again[0]), "assets/petal_frames_2/petal_frames_2_0001.png",
				"再导一次落到新目录，文件名也不撞")
		_clean(root)
	)

	t.case("asset_import_frames_rejects_empty", func() -> void:
		t.eq(AssetImport.import_frames("", "x", "n").size(), 0, "空工程目录")
		var root := _fresh_root("import_frames_empty")
		DirAccess.make_dir_recursive_absolute(root.path_join("empty"))
		t.eq(AssetImport.import_frames(root, root.path_join("empty"), "n").size(), 0,
				"目录里没有图片")
		_clean(root)
	)

	t.case("asset_import_gif_uses_ffmpeg_when_available", func() -> void:
		# GIF 拆帧依赖仓库自带的 ffmpeg（tools/ffmpeg）。它是可选依赖：没装就跳过，
		# 不让一条环境相关的用例把整个套件染红。
		var ff := AssetImport.ffmpeg_path()
		if ff.is_empty():
			return
		var root := _fresh_root("import_gif")
		var src := root.path_join("gif_src")
		DirAccess.make_dir_recursive_absolute(src)
		for i in 2:
			var img := Image.create(8, 8, false, Image.FORMAT_RGBA8)
			img.fill(Color(1, 0, 0, 1) if i == 0 else Color(0, 1, 0, 1))
			img.save_png(src.path_join("f%02d.png" % (i + 1)))
		var gif := root.path_join("anim.gif")
		var out := []
		var code := OS.execute(ff, PackedStringArray([
				"-y", "-hide_banner", "-loglevel", "error", "-framerate", "5",
				"-i", src.path_join("f%02d.png"), gif]), out, true)
		t.eq(code, 0, "生成测试用 GIF：" + "".join(out))
		if code != 0:
			_clean(root)
			return

		var res: Dictionary = AssetImport.import_gif(root, gif)
		t.eq(bool(res["ok"]), true, "拆帧成功：" + String(res["error"]))
		t.eq((res["frames"] as Array).size(), 2, "两帧都导进 assets/")
		t.approx(float(res["fps"]), 5.0, 0.6, "帧率从 ffmpeg 探测")
		_clean(root)
	)

func _fresh_root(name: String) -> String:
	var root := ProjectSettings.globalize_path("user://test_" + name)
	_clean(root)
	DirAccess.make_dir_recursive_absolute(root)
	return root


func _write(root: String, rel: String, text: String) -> String:
	var path := root.path_join(rel)
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var f := FileAccess.open(path, FileAccess.WRITE)
	f.store_string(text)
	f.close()
	return path


func _clean(path: String) -> void:
	if not DirAccess.dir_exists_absolute(path):
		return
	for f in DirAccess.get_files_at(path):
		DirAccess.remove_absolute(path.path_join(f))
	for d in DirAccess.get_directories_at(path):
		_clean(path.path_join(d))
	DirAccess.remove_absolute(path)
