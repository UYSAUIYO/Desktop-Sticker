extends SceneTree
## 把任意目录打包成**加密** PCK。用法（由 pack_encrypted.ps1 调用）：
##   godot.windows.editor.x86_64.console.exe --headless --path <临时工程> \
##       --script res://pack_encrypted.gd -- --source <目录> --out <pck> --key <64hex>
##
## 这是阶段 0 的独立验证工具：证明「PCKPacker 能写加密包」且「运行库能用烧进去的密钥读它」。
## 阶段 1 之后 Studio 自己走 src/export/packer.gd 做同样的事，这个脚本保留为排查工具。
##
## 为什么用编辑器构建跑：--script 需要能加载 GDScript 的完整脚本能力。
## 加密本身是运行时 API（PCKPacker），跟编辑器无关。

func _initialize() -> void:
	var args := _user_args()
	var source: String = args.get("source", "")
	var out: String = args.get("out", "")
	var key: String = args.get("key", "")
	if source.is_empty() or out.is_empty() or key.length() != 64:
		push_error("usage: --source <dir> --out <pck> --key <64hex>")
		quit(2)
		return

	var files := _collect(source)
	if files.is_empty():
		push_error("no files under " + source)
		quit(3)
		return

	var packer := PCKPacker.new()
	if packer.pck_start(out, 32, key, true) != OK:
		push_error("pck_start failed: " + out)
		quit(4)
		return

	var written := 0
	var prefix := "res://"
	for f in files:
		var rel: String = f.substr(source.length()).lstrip("/\\").replace("\\", "/")
		var target := prefix + rel
		if packer.add_file(target, f, true) != OK:
			push_error("add_file failed: " + target)
			quit(5)
			return
		written += 1
	if packer.flush() != OK:
		push_error("flush failed")
		quit(6)
		return

	print("[pack] %d files -> %s (encrypted files + encrypted directory index)" % [written, out])
	quit(0)


func _user_args() -> Dictionary:
	var out := {}
	var raw := OS.get_cmdline_user_args()
	var i := 0
	while i + 1 < raw.size():
		out[String(raw[i]).trim_prefix("--")] = raw[i + 1]
		i += 2
	return out


func _collect(dir_path: String) -> PackedStringArray:
	var out := PackedStringArray()
	var stack := [dir_path]
	while not stack.is_empty():
		var d: String = stack.pop_back()
		var da := DirAccess.open(d)
		if da == null:
			continue
		da.list_dir_begin()
		var entry := da.get_next()
		while entry != "":
			if entry != "." and entry != "..":
				var full := d.path_join(entry)
				if da.current_is_dir():
					stack.push_back(full)
				else:
					out.append(full.replace("\\", "/"))
			entry = da.get_next()
	return out
