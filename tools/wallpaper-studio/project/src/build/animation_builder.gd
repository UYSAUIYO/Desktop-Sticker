extends RefCounted
## 文档里的 `tracks` -> AnimationPlayer。**预览与导出共用**（和 SceneBuilder 同一条路径）。
##
## 一个求值器：编辑器拖播放头时不是自己再算一遍曲线，而是让这个播放器 `seek`。
## 预览和导出因此由构造保证一致 —— 不存在"预览有缓动、导出是匀速"这类偏差。
##
## 位置用**锚点**动，不用 position：
##   · 关键帧存的是父级比例（0..1），正好就是锚点，零换算、换分辨率不错位；
##   · 和静态图层的布局方式完全一致（见 Frame.anchors）。
##   所以 x 关键帧会展开成 anchor_left + anchor_right 两条轨道（y 同理），
##   右/下边 = 左边 + 框宽占父级的比例。
##
## scale / rotation / opacity 直接写节点属性。scale 的轴心是左上角（pivot_offset = 0），
## 与历史文档里"统一 scale 以左上角为原点"的语义一致。
##
## 缓动：Animation 的线性轨道没有逐段曲线，所以**非线性段按 60Hz 烘焙**成多个线性关键帧
## （线性段只留端点）。文档里的 `ease` 仍是唯一真源，烘焙只发生在编译这一步。
##
## 效果图层（遮罩/滤镜）不参与：它们的几何是着色器 uniform，不是节点属性，
## 动画它们需要逐帧改材质而不是改节点 —— 留到后面的阶段。

const Timeline = preload("res://src/doc/timeline.gd")
const Layer = preload("res://src/doc/layer.gd")
const Motion = preload("res://src/doc/motion.gd")

const PLAYER_NAME := "Timeline"
const ANIM_NAME := "wallpaper"
## 与 scene_builder.NODE_PREFIX 必须一致。这里不 preload scene_builder（它会 preload 本文件）。
const NODE_PREFIX := "L_"
## 缓动段烘焙密度。60Hz 与导出后的实际帧率同量级，肉眼看不出折线。
const BAKE_HZ := 60.0
## 单段最多烘焙多少个点（防止"10 分钟 + 缓动"把动画撑爆）。
const MAX_BAKE_STEPS := 180


## 给 root 挂一个 AnimationPlayer。返回 null = 文档里没有任何可动内容（不挂空播放器）。
static func compile(doc, root: Node, autoplay: bool = true) -> AnimationPlayer:
	var anim := build_animation(doc, root)
	if anim == null:
		return null
	var player := AnimationPlayer.new()
	player.name = PLAYER_NAME
	# 必须走 AnimationLibrary：AnimationMixer 没有直接 add_animation（4.x 的接口）。
	var lib := AnimationLibrary.new()
	lib.add_animation(ANIM_NAME, anim)
	player.add_animation_library("", lib)
	if autoplay:
		# 导出后由引擎自己开播；预览传 false（编辑器手动拖播放头）。
		player.autoplay = ANIM_NAME
	root.add_child(player)
	return player


## 文档 -> Animation 资源。没有可动内容时返回 null。
static func build_animation(doc, root: Node) -> Animation:
	var anim := Animation.new()
	# 长度至少 0.05：0 长度的动画在引擎里会被当成"没有内容"。
	anim.length = maxf(0.05, doc.timeline_end())
	anim.loop_mode = Animation.LOOP_LINEAR
	var added := 0
	for item in doc.walk():
		var layer: Dictionary = item["layer"]
		# 走路径的图层也算"有动画"：它的曲线在这里烘焙成 x/y 锚点轨道。
		if Layer.is_effect(layer) \
				or not (Timeline.has_animation(layer) or Motion.enabled(layer)):
			continue
		var node := root.find_child(NODE_PREFIX + String(layer.get("id", "")), true, false)
		if not (node is Control):
			continue
		# AnimationPlayer 挂在 root 下，root_node 默认是 ".."（= root）。
		var rel := root.get_path_to(node)
		if String(rel).is_empty():
			continue
		var parent_size: Vector2 = doc.parent_size(item["path"])
		# 路径启用时它接管 x/y，同名的两条通道不再单独编译（否则两边互相打架）。
		var path_samples := Motion.bake(layer, BAKE_HZ, MAX_BAKE_STEPS)
		if not path_samples.is_empty():
			added += _add_motion(anim, rel, path_samples, layer, parent_size)
		for track in Timeline.tracks_of(layer):
			var channel := String(track.get("channel", ""))
			if not path_samples.is_empty() and (channel == "x" or channel == "y"):
				continue
			added += _add_channel(anim, rel, track, layer, parent_size)
	return anim if added > 0 else null


## 关键帧被就地改过（自动关键帧 / 拖动关键帧 / 加删 key）时，把新曲线装回同一个播放器，
## 不重建节点树。返回 true = 播放器里的动画已更新。
##
## 整份重建而不是"只改那一条轨道"：轨道路径与烘焙点都可能变（改 ease、改相邻 key），
## 增量更新很容易留下旧 key。一个文档的轨道量很小，整份重建是毫秒级。
static func recompile(doc, root: Node, player: AnimationPlayer) -> bool:
	if player == null or root == null:
		return false
	var anim := build_animation(doc, root)
	if anim == null:
		return false
	var lib := AnimationLibrary.new()
	lib.add_animation(ANIM_NAME, anim)
	# 先 stop：播放器会把当前动画的求值状态缓存在 playback 里，
	# 只换库不 stop 的话，下一次 seek 仍然用旧曲线（实测：新 key 不生效）。
	player.stop()
	# 先摘掉旧库再加：同名库直接覆盖会留下旧动画的缓存。
	player.remove_animation_library("")
	player.add_animation_library("", lib)
	return true


# ---------------------------------------------------------------- 预览播放头

static func player_of(root: Node) -> AnimationPlayer:
	if root == null:
		return null
	return root.get_node_or_null(PLAYER_NAME) as AnimationPlayer


## 把播放头放到 t（没有播放器返回 false）。第一次调用会先把动画"装"上并暂停，
## 之后重复调用只做 seek —— 所以拖动时不会每帧重新起播。
static func set_player_time(player: AnimationPlayer, t: float) -> bool:
	if player == null:
		return false
	if player.current_animation != ANIM_NAME:
		player.play(ANIM_NAME)
		player.pause()
	player.seek(t, true)
	return true


# ---------------------------------------------------------------- 编译

## 一条通道 -> 一到两条属性轨道。返回插入的关键帧数量（0 = 该通道没内容）。
static func _add_channel(anim: Animation, rel: NodePath, track: Dictionary,
		layer: Dictionary, parent_size: Vector2) -> int:
	var channel := String(track.get("channel", ""))
	var keys: Array = track.get("keys", [])
	if keys.is_empty():
		return 0
	var size := Timeline.frame_size(layer, parent_size)
	var pw := maxf(1.0, parent_size.x)
	var ph := maxf(1.0, parent_size.y)
	var props := []
	match channel:
		"x":
			props = ["anchor_left", "anchor_right"]
		"y":
			props = ["anchor_top", "anchor_bottom"]
		"scale":
			props = ["scale:x", "scale:y"]
		"rotation":
			props = ["rotation_degrees"]
		"opacity":
			props = ["modulate:a"]
		_:
			return 0

	# 采样只做一次，两条轨道共用同一组时刻（左右/上下边界必须同步）。
	var samples := baked_keys(keys, channel)
	var count := 0
	for prop in props:
		var idx := anim.add_track(Animation.TYPE_VALUE)
		anim.track_set_path(idx, NodePath(String(rel) + ":" + String(prop)))
		anim.track_set_interpolation_type(idx, Animation.INTERPOLATION_LINEAR)
		# 末帧之后由引擎保持末值（Timeline 的语义），不需要额外关键帧。
		for sample in samples:
			var v := float(sample["v"])
			var value := v
			match String(prop):
				"anchor_left":
					value = v
				"anchor_right":
					value = v + size.x / pw
				"anchor_top":
					value = v
				"anchor_bottom":
					value = v + size.y / ph
			anim.track_insert_key(idx, float(sample["t"]), value)
			count += 1
	return count


## 运动路径 -> 四条锚点轨道。路径是二维的，所以这里一次产出 x/y 两组边界，
## 时刻与 _add_channel 的 x/y 完全一致（左右/上下边界必须同步）。
static func _add_motion(anim: Animation, rel: NodePath, samples: Array,
		layer: Dictionary, parent_size: Vector2) -> int:
	var size := Timeline.frame_size(layer, parent_size)
	var pw := maxf(1.0, parent_size.x)
	var ph := maxf(1.0, parent_size.y)
	# [属性, 取哪个分量, 加在后面的常量（右/下边界要加上框宽占父级的比例）]
	var specs := [
		["anchor_left", "x", 0.0],
		["anchor_right", "x", size.x / pw],
		["anchor_top", "y", 0.0],
		["anchor_bottom", "y", size.y / ph],
	]
	var count := 0
	for spec in specs:
		var idx := anim.add_track(Animation.TYPE_VALUE)
		anim.track_set_path(idx, NodePath(String(rel) + ":" + String(spec[0])))
		anim.track_set_interpolation_type(idx, Animation.INTERPOLATION_LINEAR)
		for s in samples:
			anim.track_insert_key(idx, float(s["t"]),
					float(s[String(spec[1])]) + float(spec[2]))
			count += 1
	return count


## 把一条通道的关键帧展开成"纯线性"的时刻表：
##   · 线性段 —— 只保留端点；
##   · 缓动段 —— 按 BAKE_HZ 采样，值走 Timeline.ease_value（和文档预览同一套曲线）；
##   · 首帧之前补一个 t=0 的样本（取首值），和采样器的"首帧前保持首值"一致。
## opacity 在这里夹到 0..1：预览以前是夹过的，导出也必须夹，否则两边不一致。
static func baked_keys(keys: Array, channel: String) -> Array:
	var out := []
	if keys.is_empty():
		return out
	var first: Dictionary = keys[0]
	if float(first["t"]) > 0.0:
		out.append({"t": 0.0, "v": _clamp(channel, float(first["v"]))})
	out.append({"t": float(first["t"]), "v": _clamp(channel, float(first["v"]))})
	for i in range(1, keys.size()):
		var a: Dictionary = keys[i - 1]
		var b: Dictionary = keys[i]
		var dt := float(b["t"]) - float(a["t"])
		if dt <= 0.0:
			continue
		var ease := String(b.get("ease", "linear"))
		if ease == "linear":
			out.append({"t": float(b["t"]), "v": _clamp(channel, float(b["v"]))})
			continue
		var steps := clampi(int(ceil(dt * BAKE_HZ)), 2, MAX_BAKE_STEPS)
		for s in range(1, steps + 1):
			var u := float(s) / float(steps)
			var v := lerpf(float(a["v"]), float(b["v"]), Timeline.ease_value(u, ease))
			out.append({
				"t": lerpf(float(a["t"]), float(b["t"]), u),
				"v": _clamp(channel, v),
			})
	return out


static func _clamp(channel: String, v: float) -> float:
	if channel == "opacity":
		return clampf(v, 0.0, 1.0)
	return v
