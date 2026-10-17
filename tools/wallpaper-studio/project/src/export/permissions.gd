extends RefCounted
## 从文档**保守地**推导 requested_permissions（设计 §7.1）。
##
## 推导不出来的一律不声明 —— 宁可少报也不虚报。这是纯文本扫描，不是安全分析：
## 它的唯一用途是在桌面贴纸的设置页告诉用户"这个包声明它要做什么"。
## 真正的边界是引擎侧的沙箱（.zcode/plans 的 M3b），不是这里。

const NETWORK_MARKERS := ["HTTPRequest", "HTTPClient", "WebSocketPeer", "TCPServer"]
const FILE_MARKERS := ["FileAccess", "DirAccess"]
const EXEC_MARKERS := ["OS.execute", "OS.create_process", "OS.shell_open"]
const SYSTEM_MARKERS := ["DisplayServer", "Input.parse_input_event", "OS.set_environment"]


## script_dir：用户工程目录（脚本文件的相对路径基于它）。
static func derive(doc, script_dir: String) -> Array:
	var source := _read_external_scripts(doc, script_dir)
	var out := []
	if _any(source, NETWORK_MARKERS):
		out.append("network")
	if _any(source, FILE_MARKERS):
		out.append("file_outside")
	if _any(source, EXEC_MARKERS):
		out.append("exec")
	if _any(source, SYSTEM_MARKERS):
		out.append("system")
	return out


static func _read_external_scripts(doc, script_dir: String) -> String:
	var all := ""
	for rel in doc.external_scripts():
		var path: String = rel if rel.begins_with("res://") else script_dir.path_join(rel)
		if FileAccess.file_exists(path):
			all += FileAccess.get_file_as_string(path) + "\n"
	return all


static func _any(haystack: String, needles: Array) -> bool:
	for n in needles:
		if haystack.contains(String(n)):
			return true
	return false
