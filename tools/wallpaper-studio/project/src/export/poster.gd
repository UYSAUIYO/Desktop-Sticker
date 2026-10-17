extends RefCounted
## 海报：离屏渲染主场景一帧。
##
## 与 SceneBuilder 共用构造路径 —— 所以海报和实机渲染是同一个东西缩小后的样子，
## 不存在"海报好看、实机不对"。

const SceneBuilder = preload("res://src/build/scene_builder.gd")

const SIZE := Vector2i(960, 540)


## 异步：需要等两帧让 SubViewport 真正画出来。调用方必须 await。
## 无头模式（--headless，例如 export_cli.gd 的批处理导出）直接返回 false：
## 没有渲染线程，`RenderingServer.frame_post_draw` 永远不会触发，等它就是死锁
## （实测：headless 导出卡在这里，进程不退出也不报错）。
static func render(doc, project_dir: String, out_png: String, tree: SceneTree) -> bool:
	if DisplayServer.get_name() == "headless":
		return false
	var sub := SubViewport.new()
	sub.size = SIZE
	sub.transparent_bg = false
	sub.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	sub.own_world_3d = true
	sub.render_target_clear_mode = SubViewport.CLEAR_MODE_ALWAYS
	sub.disable_3d = true
	tree.root.add_child(sub)

	var root = SceneBuilder.build(doc, project_dir)
	# 海报是固定 960x540，把文档画布等比缩进去并居中；设计画布和取景框比例不同时
	# 只留边，不拉伸内容。
	#
	# 先摘掉「铺满父级」的锚点：SceneBuilder 给的是全屏锚点，那是给预览/壁纸窗口用的；
	# 海报要的是"保持 doc.canvas 的固有尺寸再整体缩放"，锚点留在全屏会被父级
	# 拉伸成 960x540（比例就错了）。keep_offsets=true 保留当前矩形。
	var scale := minf(float(SIZE.x) / float(doc.canvas.x), float(SIZE.y) / float(doc.canvas.y))
	root.set_anchors_preset(Control.PRESET_TOP_LEFT, true)
	root.size = Vector2(doc.canvas)
	root.scale = Vector2(scale, scale)
	root.position = (Vector2(SIZE) - Vector2(doc.canvas) * scale) * 0.5
	sub.add_child(root)

	# 一帧让节点进树并完成布局，一帧让渲染目标真的画出来。
	await tree.process_frame
	await tree.process_frame
	await RenderingServer.frame_post_draw
	var img := sub.get_texture().get_image()
	sub.queue_free()
	if img == null:
		return false
	return img.save_png(out_png) == OK
