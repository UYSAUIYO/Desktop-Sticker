extends RefCounted
## 加密 PCK + .dswall 容器。
##
## 密钥来自 <studio>/signing_key.txt，与 tools/build_godot.ps1 烧进 libgodot 的
## script_encryption_key 是**同一个值**。引擎读包时用编译进去的密钥，宿主不需要传密钥。
## 两边对不上时的症状是运行库报
## "The MD5 sum of the decrypted file does not match the expected value"。

const Permissions = preload("res://src/export/permissions.gd")


static func load_key() -> String:
	var path := "res://signing_key.txt"
	if not FileAccess.file_exists(path):
		return ""
	var key := FileAccess.get_file_as_string(path).strip_edges()
	if key.length() != 64 or not key.is_valid_hex_number(false):
		return ""
	if key == "0".repeat(64):
		# 全 0 在引擎里表示"未启用加密"，用它打出来的包运行库读不了。
		return ""
	return key


## 把 disk_dir 下所有文件按 res://<res_prefix>/<rel> 打进一个加密 PCK。
## encrypt_directory 让文件索引也是密文 —— 解压工具连文件名都看不到。
static func pack_encrypted(disk_dir: String, res_prefix: String, out_pck: String, key_hex: String) -> Error:
	var files := _walk(disk_dir)
	if files.is_empty():
		return ERR_FILE_NOT_FOUND
	var packer := PCKPacker.new()
	var err := packer.pck_start(out_pck, 32, key_hex, true)
	if err != OK:
		return err
	var base := disk_dir.replace("\\", "/").trim_suffix("/")
	for f in files:
		var rel: String = f.substr(base.length()).trim_prefix("/")
		var target := "res://" + res_prefix.trim_suffix("/").path_join(rel)
		err = packer.add_file(target, f, true)
		if err != OK:
			return err
	return packer.flush()


## 把三个文件装成 .dswall（ZIP）。先写 .tmp 再改名，保证不会产生半个包。
## poster_path 为空或不存在时跳过 poster.png 条目。
static func assemble_dswall(out_path: String, pck_path: String, manifest_text: String, poster_path: String) -> Error:
	var tmp := out_path + ".tmp"
	if FileAccess.file_exists(tmp):
		DirAccess.remove_absolute(tmp)
	var zip := ZIPPacker.new()
	if zip.open(tmp) != OK:
		return ERR_CANT_CREATE
	var ok := true
	ok = _add(zip, "project.pck", pck_path) and ok
	ok = _add_text(zip, "manifest.json", manifest_text) and ok
	if not poster_path.is_empty() and FileAccess.file_exists(poster_path):
		ok = _add(zip, "poster.png", poster_path) and ok
	zip.close()
	if not ok:
		DirAccess.remove_absolute(tmp)
		return ERR_CANT_CREATE
	if FileAccess.file_exists(out_path):
		DirAccess.remove_absolute(out_path)
	return DirAccess.rename_absolute(tmp, out_path)


static func _add(zip: ZIPPacker, name: String, disk_path: String) -> bool:
	var bytes := FileAccess.get_file_as_bytes(disk_path)
	if bytes.is_empty():
		return false
	zip.start_file(name)
	return zip.write_file(bytes) == OK


static func _add_text(zip: ZIPPacker, name: String, text: String) -> bool:
	zip.start_file(name)
	return zip.write_file(text.to_utf8_buffer()) == OK


static func _walk(dir_path: String) -> PackedStringArray:
	var out := PackedStringArray()
	var stack := [dir_path]
	while not stack.is_empty():
		var d: String = stack.pop_back()
		var da := DirAccess.open(d)
		if da == null:
			continue
		for entry in da.get_files():
			out.append(d.path_join(entry).replace("\\", "/"))
		for sub in da.get_directories():
			stack.push_back(d.path_join(sub))
	return out
