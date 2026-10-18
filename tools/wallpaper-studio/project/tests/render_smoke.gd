extends SceneTree
## 渲染冒烟：把带遮罩 / 滤镜 / 模糊 / 容器遮罩的图层**真的渲染几帧**，让渲染器去编译
## 那些 canvas_item 着色器。无头测试跑在 dummy 驱动上，着色器根本不会被编译 ——
## 语法错、uniform 类型不对这类问题只有这里能抓到。
##
## 用法（离屏小窗，几秒后自己退出）：
##   dstk-studio.exe --script res://tests/render_smoke.gd --position 3000,3000 --resolution 320x180
## 判据：输出里有 PROBE RENDER OK，且**没有** "SHADER ERROR" / "Shader compilation failed"。
##
## 名字不带 test_ 前缀是有意的：它需要真实渲染器，不能进无头测试集。

const StudioDoc = preload("res://src/doc/doc.gd")
const SceneBuilder = preload("res://src/build/scene_builder.gd")
const Layer = preload("res://src/doc/layer.gd")
const Mask = preload("res://src/doc/mask.gd")
const Filters = preload("res://src/doc/filters.gd")

const MASK_PNG := "user://mask_probe.png"


## SceneTree 脚本没有 _ready()：入口是 _initialize()，和 tests/run_tests.gd 一样。
func _initialize() -> void:
	await process_frame

	var doc = StudioDoc.create_empty("probe", Vector2i(640, 360))

	# 1) 圆角矩形遮罩 + 色彩 + 暗角 + 阴影
	var block := Layer.make_color("块", Color(1, 0.4, 0.2))
	block["transform"]["w"] = 400.0
	block["transform"]["h"] = 200.0
	block["mask"] = Mask.normalize({"enabled": true, "shape": "rounded",
			"w": 320.0, "h": 160.0, "radius": 40.0, "feather": 10.0})
	var f := Filters.normalize({})
	f[Filters.COLOR]["enabled"] = true
	f[Filters.VIGNETTE]["enabled"] = true
	f[Filters.SHADOW]["enabled"] = true
	block["filters"] = f
	doc.add_layer(block)

	# 2) 文字 + 模糊 + 色调分离
	var title := Layer.make_text("文", "遮罩/滤镜")
	var f2 := Filters.normalize({})
	f2[Filters.BLUR]["enabled"] = true
	f2[Filters.BLUR]["radius"] = 8.0
	f2[Filters.POSTERIZE]["enabled"] = true
	title["filters"] = f2
	doc.add_layer(title)

	# 3) 图片遮罩（真的采样一张 alpha 贴图）+ 相加混合
	var img := Image.create(64, 64, false, Image.FORMAT_RGBA8)
	for y in 64:
		for x in 64:
			img.set_pixel(x, y, Color(1, 1, 1, 1.0 if (x + y) % 2 == 0 else 0.2))
	img.save_png(MASK_PNG)
	var masked := Layer.make_image("图", MASK_PNG.get_file())
	masked["transform"]["x"] = 180.0
	masked["transform"]["y"] = 80.0
	masked["transform"]["w"] = 300.0
	masked["transform"]["h"] = 200.0
	masked["mask"] = Mask.normalize({"enabled": true, "shape": "image",
			"src": MASK_PNG.get_file(), "w": 300.0, "h": 200.0})
	masked["blend"] = "add"
	doc.add_layer(masked)

	# 4) 容器自带椭圆遮罩（走 SubViewport 包裹那条路）
	var inner := Layer.make_color("内", Color(0.2, 0.8, 1.0))
	inner["transform"]["w"] = 200.0
	inner["transform"]["h"] = 100.0
	var group := Layer.make_group("容器", 400.0, 200.0)
	group["transform"]["x"] = 100.0
	group["transform"]["y"] = 140.0
	group["children"] = [inner]
	group["mask"] = Mask.normalize({"enabled": true, "shape": "ellipse",
			"w": 400.0, "h": 200.0, "feather": 6.0})
	doc.add_layer(group)

	var root: Control = SceneBuilder.build(doc, "user://")
	get_root().add_child(root)
	for i in 6:
		await process_frame
	print("PROBE RENDER OK")
	quit(0)
