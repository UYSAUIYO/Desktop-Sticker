extends RefCounted
## 撤销/重做：**整份文档的快照栈**。
##
## 为什么是快照而不是"反向操作"：文档的改动入口太多（属性面板、画布拖动、时间轴、
## 图层增删、容器进出……），给每一处都写一对 do/undo 必然漏，而且漏掉的那处会静默
## 污染后续历史。一个壁纸工程的 JSON 通常只有几十 KB，整份存下来很便宜。
##
## 两个必要的收敛规则，否则历史会被噪音灌满：
##   · 内容相同（text 相同）只更新选中，不记一步 —— 面板刷新也走 apply_change；
##   · 连续同类操作在 MERGE_MS 内合并成一步 —— 拖滑块 / 连续拖动关键帧是常态，
##     逐帧记一步的话撤一次只退一点点。
##
## 栈里存的是"做完这步之后的样子"，所以 undo() 弹出当前、返回上一项；
## 被弹出的那一顶 push 到 redo 栈。

const MAX_STEPS := 80
## 同一标签在这个毫秒数内的连续提交合并成一步。
const MERGE_MS := 700

var _undo: Array = []
var _redo: Array = []
var _last_label := ""
var _last_ms := 0


## 换工程 / 新建：历史从这一刻的文档重新开始。
func reset(state: Dictionary) -> void:
	_undo = [state]
	_redo = []
	_last_label = ""
	_last_ms = 0


func push(state: Dictionary, label: String) -> void:
	if _undo.is_empty():
		_undo.append(state)
		return
	var top: Dictionary = _undo[_undo.size() - 1]
	if String(top.get("text", "")) == String(state.get("text", "")):
		# 内容没变：只把选中状态更新一下，不占一步历史。
		_undo[_undo.size() - 1] = state
		return
	var now := Time.get_ticks_msec()
	if label == _last_label and now - _last_ms <= MERGE_MS:
		_undo[_undo.size() - 1] = state
	else:
		_undo.append(state)
		while _undo.size() > MAX_STEPS:
			_undo.remove_at(0)
	_last_label = label
	_last_ms = now
	_redo.clear()


func can_undo() -> bool:
	return _undo.size() > 1


func can_redo() -> bool:
	return not _redo.is_empty()


## 返回要恢复到的状态（空 = 没有可撤销的）。
func undo() -> Dictionary:
	if not can_undo():
		return {}
	var current: Dictionary = _undo.pop_back()
	_redo.append(current)
	return _undo[_undo.size() - 1]


## 返回要恢复到的状态（空 = 没有可重做的）。
func redo() -> Dictionary:
	if _redo.is_empty():
		return {}
	var state: Dictionary = _redo.pop_back()
	_undo.append(state)
	return state


## 下一步会提交时打破合并：调用方要开始一段**新的**连续操作（比如鼠标按下）。
func break_merge() -> void:
	_last_label = ""
	_last_ms = 0


func depth() -> int:
	return _undo.size()
