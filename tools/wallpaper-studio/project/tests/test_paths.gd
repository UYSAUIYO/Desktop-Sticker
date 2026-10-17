extends RefCounted
## paths.gd 的用例。
##
## 注册机制：run_tests.gd 遍历 res://tests/test_*.gd，实例化后调用 register()。
## 刻意**不用** static func _static_init()：那个的触发时机跟脚本加载/类注册绑在一起，
## 在无编辑器的运行库里并不可靠；显式调用 register() 是确定的。

const StudioPaths = preload("res://src/doc/paths.gd")


func register(t) -> void:
	t.case("paths_build_dir_is_under_res", func() -> void:
		t.eq(StudioPaths.build_dir("abc123"), "res://build/abc123", "build dir")
	)
	t.case("paths_scratch_dir_under_user", func() -> void:
		t.ok(StudioPaths.scratch_dir("abc123").begins_with("user://"), "scratch under user://")
	)
	t.case("paths_new_pack_id_is_32_hex", func() -> void:
		var id = StudioPaths.new_pack_id()
		t.eq(id.length(), 32, "pack id length")
		t.ok(id == id.to_lower() and id.is_valid_hex_number(false), "pack id is lowercase hex")
		t.ok(StudioPaths.new_pack_id() != id, "pack ids differ")
	)
	t.case("paths_dswall_name_sanitized", func() -> void:
		t.eq(StudioPaths.dswall_file_name("我的/壁纸:"), "我的_壁纸_.dswall", "illegal chars replaced")
		t.eq(StudioPaths.dswall_file_name("   "), "wallpaper.dswall", "blank falls back")
		t.eq(StudioPaths.dswall_file_name("rain"), "rain.dswall", "plain name kept")
	)
	t.case("paths_exports_dir_under_user", func() -> void:
		t.ok(StudioPaths.exports_dir().begins_with("user://"), "exports under user://")
		t.ok(StudioPaths.exports_dir() != StudioPaths.default_project_root(), "exports split from projects")
	)
