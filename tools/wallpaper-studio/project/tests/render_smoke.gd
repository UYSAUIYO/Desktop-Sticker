extends SceneTree
## 渲染冒烟：把带遮罩层 / 滤镜层 / 模糊 / 嵌套合成的文档**真的渲染几帧**，
## 让渲染器去编译那些 canvas_item 着色器。无头测试跑在 dummy 驱动上，
## 着色器根本不会被编译 —— 语法错、uniform 类型不对这类问题只有这里能抓到。
##
## 用法（离屏小窗，几秒后自己退出）：
##   dstk-studio.exe --script res://tests/render_smoke.gd --position 3000,3000 --resolution 320x180
## 判据：输出里有 PROBE RENDER OK，且**没有** "SHADER ERROR" / "Shader compilation failed"。
##
## 名字不带 test_ 前缀是有意的：它需要真实渲染器，不能进无头测试集。

const StudioDoc = preload("res://src/doc/doc.gd")
const SceneBuilder = preload("res://src/build/scene_builder.gd")
const Layer = preload("res://src/doc/layer.gd")
const Filters = preload("res://src/doc/filters.gd")

const MASK_PNG := "user://mask_probe.png"


## SceneTree 脚本没有 _ready()：入口是 _initialize()，和 tests/run_tests.gd 一样。
func _initialize() -> void:
	await process_frame

	var doc = StudioDoc.create_empty("probe", Vector2i(640, 360))

	# 1) 色块 + 文字 -> 圆角遮罩层（覆盖 2 层）-> 滤镜层（色彩/暗角/阴影，覆盖 1 层）
	var block := Layer.make_color("块", Color(1, 0.4, 0.2))
	block["transform"]["w"] = 400.0
	block["transform"]["h"] = 200.0
	doc.add_layer(block)
	doc.add_layer(Layer.make_text("文", "遮罩/滤镜"))

	var m1 := Layer.make_mask("圆角遮罩")
	m1["shape"] = "rounded"
	m1["radius"] = 40.0
	m1["feather"] = 10.0
	m1["transform"]["w"] = 480.0
	m1["transform"]["h"] = 260.0
	m1["cover"] = {"mode": "count", "count": 2}
	doc.add_layer(m1)

	var f1 := Layer.make_filter("调色")
	f1["filters"][Filters.COLOR]["enabled"] = true
	f1["filters"][Filters.VIGNETTE]["enabled"] = true
	f1["filters"][Filters.SHADOW]["enabled"] = true
	f1["cover"] = {"mode": "count", "count": 1}
	doc.add_layer(f1)

	# 2) 图片 + 图片 alpha 遮罩层（真的采样一张贴图）
	var img := Image.create(64, 64, false, Image.FORMAT_RGBA8)
	for y in 64:
		for x in 64:
			img.set_pixel(x, y, Color(1, 1, 1, 1.0 if (x + y) % 2 == 0 else 0.2))
	img.save_png(MASK_PNG)
	var photo := Layer.make_image("图", MASK_PNG.get_file())
	photo["transform"]["x"] = 180.0
	photo["transform"]["y"] = 80.0
	photo["transform"]["w"] = 300.0
	photo["transform"]["h"] = 200.0
	photo["blend"] = "add"
	doc.add_layer(photo)

	var m2 := Layer.make_mask("图片遮罩")
	m2["shape"] = "image"
	m2["src"] = MASK_PNG.get_file()
	m2["transform"]["x"] = 180.0
	m2["transform"]["y"] = 80.0
	m2["transform"]["w"] = 300.0
	m2["transform"]["h"] = 200.0
	m2["cover"] = {"mode": "count", "count": 1}
	doc.add_layer(m2)

	# 3) 容器：子层 + 容器内的滤镜层（嵌套合成）；容器外再套一层调色滤镜
	var inner := Layer.make_color("内", Color(0.2, 0.8, 1.0))
	inner["transform"]["w"] = 200.0
	inner["transform"]["h"] = 100.0
	var f3 := Layer.make_filter("容器内模糊")
	f3["filters"][Filters.BLUR]["enabled"] = true
	f3["filters"][Filters.BLUR]["radius"] = 6.0
	f3["filters"][Filters.POSTERIZE]["enabled"] = true
	f3["cover"] = {"mode": "count", "count": 1}
	var group := Layer.make_group("容器", 400.0, 200.0)
	group["transform"]["x"] = 100.0
	group["transform"]["y"] = 140.0
	group["children"] = [inner, f3]
	doc.add_layer(group)

	var f4 := Layer.make_filter("容器调色")
	f4["filters"][Filters.COLOR]["enabled"] = true
	f4["filters"][Filters.COLOR]["saturation"] = 1.4
	f4["cover"] = {"mode": "count", "count": 1}
	doc.add_layer(f4)

	var root: Control = SceneBuilder.build(doc, "user://")
	get_root().add_child(root)
	for i in 6:
		await process_frame
	print("PROBE RENDER OK")
	quit(0)
