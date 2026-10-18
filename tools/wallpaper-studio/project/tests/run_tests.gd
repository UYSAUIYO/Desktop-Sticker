extends SceneTree
## Studio 无头测试入口。
##
##   dstk-studio.exe --headless --script res://tests/run_tests.gd
##   dstk-studio.exe --headless --script res://tests/run_tests.gd -- --only=doc_roundtrip
##
## 退出码：0 全通过，1 有用例失败，2 是收集阶段就不对（没有测试文件等）。
##
## 收集机制：遍历 res://tests/test_*.gd，实例化后调用它的 register(collector)。
## 刻意不用 static func _static_init()：它的触发时机和脚本加载/类注册绑在一起，
## 在无编辑器的 template_release 运行库里不可靠；显式调用才是确定的。
##
## collector 是**实例**而不是 static var —— 原因见 test_framework.gd 顶部的说明
## （preload 脚本里的 static var 会让进程在 shutdown 时堆损坏）。

const DTest = preload("res://tests/test_framework.gd")


func _initialize() -> void:
	# 等第一帧结束再跑。编辑器装配用例要把 main.tscn 挂进树，而节点的 _ready
	# 是引擎在帧边界统一触发的：在 _initialize 里同步 add_child 会撞上
	# "Parent node is busy setting up children"，界面根本没建起来。
	# 纯逻辑用例不关心这一帧。
	await process_frame
	_collect_and_run()


func _collect_and_run() -> void:
	var dir := DirAccess.open("res://tests")
	if dir == null:
		printerr("cannot open res://tests")
		quit(2)
		return

	var collector = DTest.new()
	var loaded := 0
	for f in dir.get_files():
		var file_name := f.trim_suffix(".remap")
		if not file_name.begins_with("test_") or not file_name.ends_with(".gd"):
			continue
		var path := "res://tests/" + file_name
		var res := load(path)
		if res == null:
			printerr("failed to load " + path)
			quit(2)
			return
		# 每个测试文件一个独立实例，避免用例之间通过成员变量串味。
		res.new().register(collector)
		loaded += 1
	if loaded == 0:
		printerr("no test_*.gd found under res://tests")
		quit(2)
		return

	# Godot 会把 `--key=value` 拆成两个 user arg（"--key" 和 "value"），所以这里按
	# "键 值"成对解析，不要写 begins_with("--only=") 那种形式 —— 那个永远匹配不上。
	var user_args := OS.get_cmdline_user_args()
	var only := ""
	var i := 0
	while i < user_args.size():
		if user_args[i] == "--only" and i + 1 < user_args.size():
			only = user_args[i + 1]
			break
		elif String(user_args[i]).begins_with("--only="):
			only = String(user_args[i]).trim_prefix("--only=")
			break
		i += 1

	print("collected %d test files, %d cases" % [loaded, collector.case_names().size()])
	var code := collector.run_all(only)
	collector.dispose()
	quit(code)
