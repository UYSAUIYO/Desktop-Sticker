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
