extends SceneTree
## 无头导出：把一个 .dswproj 目录导出成 .dswall。
##
##   dstk-studio.exe --headless --script res://src/cli/export_cli.gd -- \
##       --project <工程目录> --out <输出.dswall> [--inbox]
##
## 用途：CI/批处理、以及不打开界面就能验证整条导出链路。
## （--inbox 会在导出后投递到桌面贴纸收件箱，做热应用。）
##
## 为什么导出放在 _process 里而不是 _initialize：海报要 await 两帧 + frame_post_draw，
## _initialize 是从 C++ 调进来的，在里面 await 不安全；把活挪到主循环的第一帧最稳。

const StudioDoc = preload("res://src/doc/doc.gd")
const Exporter = preload("res://src/export/exporter.gd")
const Poster = preload("res://src/export/poster.gd")
const StudioPaths = preload("res://src/doc/paths.gd")

var _project_dir := ""
var _out_path := ""
var _deliver := false
var _started := false


func _initialize() -> void:
	var args := _user_args()
	_project_dir = args.get("project", "")
	_out_path = args.get("out", "")
	_deliver = args.has("inbox")
	if _project_dir.is_empty() or _out_path.is_empty():
		printerr("usage: --project <dir> --out <file.dswall> [--inbox]")
		quit(2)


## Godot 会把 `--key=value` 拆成两个 user arg，所以按"键 值"成对解析。
func _user_args() -> Dictionary:
	var out := {}
	var raw := OS.get_cmdline_user_args()
	var i := 0
	while i < raw.size():
		var a := String(raw[i])
		if a.begins_with("--") and i + 1 < raw.size():
			out[a.trim_prefix("--")] = String(raw[i + 1])
			i += 2
		else:
			if a.begins_with("--"):
				out[a.trim_prefix("--")] = ""
			i += 1
	return out


func _process(_delta: float) -> bool:
	if not _started:
		_started = true
		# 关键：**不要 await、也不要在 _process 里返回 true**。
		# 让 _run() 作为独立协程挂在信号上跑，跑完自己 quit()；
		# 而 _process 是协程时引擎拿到的返回值不是 bool，控制流会直接丢掉。
		_run()
	return false


func _run() -> void:
	var manifest_path := _project_dir.path_join("wallpaper.json")
	if not FileAccess.file_exists(manifest_path):
		printerr("no wallpaper.json in " + _project_dir)
		quit(3)
		return

	var parsed: Dictionary = StudioDoc.parse(FileAccess.get_file_as_string(manifest_path))
	if not parsed.ok:
		printerr("cannot load project: " + String(parsed.error))
		quit(3)
		return
	var doc = parsed.doc
	print("[export] '%s' canvas=%s layers=%d" % [doc.name, doc.canvas, doc.layers.size()])

	# 海报先渲染 —— Exporter 是同步的，海报是它的入参。
	var scratch := ProjectSettings.globalize_path(StudioPaths.scratch_dir(doc.pack_id))
	DirAccess.make_dir_recursive_absolute(scratch)
	var poster_path := scratch.path_join("poster.png")
	var poster_ok: bool = await Poster.render(doc, _project_dir, poster_path, self)
	print("[export] poster rendered: ", poster_ok)

	var result: Dictionary = Exporter.export(doc, _project_dir, _out_path, poster_path)
	if not result.ok:
		printerr("[export] FAILED: " + String(result.error))
		for p in result.problem_list:
			printerr("  - " + String(p))
		quit(1)
		return

	print("[export] ok -> ", result.path)

	if _deliver:
		var sent: Dictionary = Exporter.deliver_to_inbox(result.path)
		if sent.ok:
			print("[export] delivered -> ", sent.path)
		else:
			printerr("[export] delivery failed: " + String(sent.error))
			quit(1)
			return
	quit(0)
