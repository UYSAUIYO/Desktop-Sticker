extends RefCounted
## 滤镜（Filters）：固定 7 组，每组一个开关 + 自己的参数。
## 这组参数属于**滤镜层**（type = filter）—— 它是独立图层，作用在下方被覆盖的图层上，
## 不是某个图层自己的属性（见 doc/layer.gd 的 EFFECT_TYPES）。
##
## 为什么不是"可任意排序的效果栈"：色彩 / 暗角 / 色调分离都是逐像素运算，顺序固定
## （色彩 → 暗角 → 色调分离）就够用；阴影 / 发光 / 描边是"内容下面多画几份"，
## 天然在内容之下。固定结构换来的好处是属性面板与文档结构都简单，且着色器只有一个。
##
## 实现分两条路：
##   · 色彩 / 暗角 / 色调分离 / 遮罩 —— 一个 canvas_item 着色器，逐像素乘算，零额外开销；
##   · 模糊 —— 需要采样邻域，用两趟 SubViewport + 可分离高斯（见 layer_nodes.gd）；
##   · 阴影 / 发光 / 描边 —— 复制被覆盖的图层、用 modulate 染色后垫在下面（发光那份过一趟模糊）。

const COLOR := "color"
const BLUR := "blur"
const VIGNETTE := "vignette"
const POSTERIZE := "posterize"
const SHADOW := "shadow"
const GLOW := "glow"
const STROKE := "stroke"

## 滤镜层的**作用区域**形状。和遮罩层同一套 SDF（见着色器里的 shape_alpha），
## 但不提供"图片 alpha" —— 区域只是"哪儿生效"的框，没必要再引一张贴图。
const REGION_SHAPES := ["rect", "rounded", "ellipse"]

## 参数默认值。enabled=false 的那组在着色器里就是"恒等运算"。
static func defaults() -> Dictionary:
	return {
		"color": {"enabled": false, "brightness": 0.0, "contrast": 1.0,
				"saturation": 1.0, "hue": 0.0},
		"blur": {"enabled": false, "radius": 12.0},
		"vignette": {"enabled": false, "amount": 0.5, "radius": 0.65, "softness": 0.45},
		"posterize": {"enabled": false, "levels": 8.0},
		"shadow": {"enabled": false, "x": 14.0, "y": 14.0,
				"color": Color(0, 0, 0), "alpha": 0.45},
		"glow": {"enabled": false, "radius": 20.0,
				"color": Color(0.6, 0.85, 1.0), "alpha": 0.55},
		"stroke": {"enabled": false, "width": 4.0, "color": Color.WHITE},
	}


static func normalize(raw) -> Dictionary:
	var out := defaults()
	if not (raw is Dictionary):
		return out
	for key in out.keys():
		var src = raw.get(key)
		if not (src is Dictionary):
			continue
		out[key]["enabled"] = bool(src.get("enabled", false))
		match key:
			COLOR:
				for k in ["brightness", "contrast", "saturation", "hue"]:
					out[key][k] = float(src.get(k, out[key][k]))
			BLUR:
				out[key]["radius"] = maxf(0.0, float(src.get("radius", 12.0)))
			VIGNETTE:
				for k in ["amount", "radius", "softness"]:
					out[key][k] = float(src.get(k, out[key][k]))
			POSTERIZE:
				out[key]["levels"] = clampf(float(src.get("levels", 8.0)), 2.0, 64.0)
			SHADOW:
				out[key]["x"] = float(src.get("x", 14.0))
				out[key]["y"] = float(src.get("y", 14.0))
				out[key]["color"] = _to_color(src.get("color"), Color(0, 0, 0))
				out[key]["alpha"] = float(src.get("alpha", 0.45))
			GLOW:
				out[key]["radius"] = maxf(0.0, float(src.get("radius", 20.0)))
				out[key]["color"] = _to_color(src.get("color"), Color(0.6, 0.85, 1.0))
				out[key]["alpha"] = float(src.get("alpha", 0.55))
			STROKE:
				out[key]["width"] = maxf(0.0, float(src.get("width", 4.0)))
				out[key]["color"] = _to_color(src.get("color"), Color.WHITE)
	return out


# ---------------------------------------------------------------- 作用区域
##
## 滤镜层的**作用区域**：框 + 形状，几何存在图层自己的 `transform` 里（和遮罩层、
## 和普通元素的选区框是同一个模型），所以画布上可以直接拖/缩放、角点 Shift 等比。
##   · enabled = false（默认）—— 作用在被覆盖图层的整个区域上（老行为）；
##   · enabled = true        —— 只有框内是滤镜后的样子，框外保持原样。
## 形状只有矩形/圆角矩形/椭圆：区域是"哪儿生效"，不需要图片 alpha。
static func region_defaults() -> Dictionary:
	return {"enabled": false, "shape": "rect", "radius": 24.0, "feather": 0.0,
			"invert": false}


static func normalize_region(raw) -> Dictionary:
	var out := region_defaults()
	if not (raw is Dictionary):
		return out
	out["enabled"] = bool(raw.get("enabled", false))
	var shape := String(raw.get("shape", "rect"))
	out["shape"] = shape if shape in REGION_SHAPES else "rect"
	out["radius"] = maxf(0.0, float(raw.get("radius", out["radius"])))
	out["feather"] = maxf(0.0, float(raw.get("feather", out["feather"])))
	out["invert"] = bool(raw.get("invert", false))
	return out


## 滤镜层的作用区域参数（缺字段按默认；不是滤镜层时也返回默认值）。
## 返回的是**新字典**：要改就写回 layer["region"]（属性面板就是这么做的）。
static func region(layer: Dictionary) -> Dictionary:
	return normalize_region(layer.get("region"))


## 这一层有没有"限制作用区域"（只有滤镜层可能为 true）。
static func region_enabled(layer: Dictionary) -> bool:
	return bool(region(layer).get("enabled", false))


static func _to_color(v, fallback: Color) -> Color:
	if v is Color:
		return v
	if v is String:
		var s: String = v.strip_edges()
		if s.begins_with("#"):
			s = s.substr(1)
			if (s.length() == 6 or s.length() == 8) and s.is_valid_hex_number(false):
				return Color.html(s)
			return fallback
		# 同 Layer.to_color：兜底 Color 被 JSON 直接 str() 出来的老写法。
		if s.begins_with("(") and s.ends_with(")"):
			var parts := s.substr(1, maxi(0, s.length() - 2)).split(",", false)
			if parts.size() >= 3:
				return Color(float(parts[0]), float(parts[1]), float(parts[2]),
						float(parts[3]) if parts.size() > 3 else 1.0)
	return fallback


static func on(f: Dictionary, key: String) -> bool:
	var g = f.get(key)
	return g is Dictionary and bool(g.get("enabled", false))


## 任意一组开着吗（图层列表用它提示"这层滤镜还没启用任何项"）。
static func any(f: Dictionary) -> bool:
	for key in defaults().keys():
		if on(f, key):
			return true
	return false


## Rect2 -> Vector4（着色器的 vec4 uniform）。
static func _as_vec4(r: Rect2) -> Vector4:
	return Vector4(r.position.x, r.position.y, r.size.x, r.size.y)


## 需要逐像素着色器吗（色彩/暗角/色调分离）—— 遮罩也走同一个着色器，由调用方一并判断。
static func needs_material(f: Dictionary) -> bool:
	return on(f, COLOR) or on(f, VIGNETTE) or on(f, POSTERIZE)


static func blur_radius(f: Dictionary) -> float:
	if not on(f, BLUR):
		return 0.0
	return maxf(0.0, float(f[BLUR].get("radius", 0.0)))


static func needs_copy(f: Dictionary) -> bool:
	return on(f, SHADOW) or on(f, GLOW) or on(f, STROKE)


## 逐像素那套的 uniform。遮罩/画布映射由调用方补齐。
## 注意：着色器 uniform 只认有限类型 —— Rect2 **不行**，几何一律压成 Vector4 传
## （set_shader_parameter 收到 Rect2 会直接崩，踩过）。
static func grade_uniforms(f: Dictionary, layer_uv: Rect2) -> Dictionary:
	var out := {
		"brightness": 0.0, "contrast": 1.0, "saturation": 1.0, "hue": 0.0,
		"vignette_amount": 0.0, "vignette_radius": 0.65, "vignette_softness": 0.45,
		"posterize_levels": 0.0,
		"layer_uv": _as_vec4(layer_uv),
	}
	if on(f, COLOR):
		var g: Dictionary = f[COLOR]
		out["brightness"] = float(g.get("brightness", 0.0))
		out["contrast"] = float(g.get("contrast", 1.0))
		out["saturation"] = float(g.get("saturation", 1.0))
		out["hue"] = deg_to_rad(float(g.get("hue", 0.0)))
	if on(f, VIGNETTE):
		var v: Dictionary = f[VIGNETTE]
		out["vignette_amount"] = clampf(float(v.get("amount", 0.5)), 0.0, 1.0)
		out["vignette_radius"] = float(v.get("radius", 0.65))
		out["vignette_softness"] = maxf(0.001, float(v.get("softness", 0.45)))
	if on(f, POSTERIZE):
		out["posterize_levels"] = clampf(float(f[POSTERIZE].get("levels", 8.0)), 2.0, 64.0)
	return out


## 形状 SDF（矩形 0 / 圆角 1 / 椭圆 2）+ 滤镜的**作用区域**。
##
## 遮罩层和"滤镜作用区域"共用这一份：两处的圆角/羽化算法必须一模一样，否则
## 同一个框套上遮罩与套上滤镜区域会差一点点，用户没法对齐。
## 坐标 p 是**形状框自己归一化后的比例**（0..1），所以窗口尺寸、容器嵌套都不影响。
## region_shape < 0 = 不限制区域。
const REGION_BODY := """
uniform int region_shape = -1;
uniform vec4 region_rect = vec4(0.0, 0.0, 1.0, 1.0);
uniform vec2 region_radii = vec2(0.0, 0.0);
uniform vec2 region_feather = vec2(0.0, 0.0);
uniform bool region_invert = false;

float shape_alpha(vec2 p, int shape, vec2 radii, vec2 feather) {
	float d;
	if (shape == 2) {
		vec2 q = (p - 0.5) * 2.0;
		d = (length(q) - 1.0) * 0.5;
	} else {
		vec2 q = abs(p - 0.5) - (vec2(0.5) - radii);
		d = min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - min(radii.x, radii.y);
	}
	float e = max(max(feather.x, feather.y), 0.0005);
	return 1.0 - smoothstep(-e, e, d);
}

float region_alpha(vec2 cuv) {
	if (region_shape < 0) {
		return 1.0;
	}
	vec2 p = (cuv - region_rect.xy) / max(region_rect.zw, vec2(0.000001));
	float a = shape_alpha(p, region_shape, region_radii, region_feather);
	return region_invert ? 1.0 - a : a;
}
"""

const LAYER_BODY := """
uniform int mask_shape = -1;
uniform vec4 mask_rect = vec4(0.0, 0.0, 1.0, 1.0);
uniform vec2 mask_radii = vec2(0.0, 0.0);
uniform vec2 mask_feather = vec2(0.0, 0.0);
uniform bool mask_invert = false;
uniform sampler2D mask_tex : filter_linear, repeat_disable;

// region_blend = true：区域外是**原图**（逐像素滤镜/模糊的语义，看到的是"没处理的样子"）；
// false：只按区域裁剪 alpha（复制类滤镜的副本用它，副本不该画到框外）。
uniform bool region_blend = true;

uniform float brightness = 0.0;
uniform float contrast = 1.0;
uniform float saturation = 1.0;
uniform float hue = 0.0;

uniform vec4 layer_uv = vec4(0.0, 0.0, 1.0, 1.0);
uniform float vignette_amount = 0.0;
uniform float vignette_radius = 0.65;
uniform float vignette_softness = 0.45;
uniform float posterize_levels = 0.0;
""" + REGION_BODY + """

vec3 hue_rotate(vec3 c, float a) {
	vec3 k = vec3(0.57735026919);
	float cs = cos(a);
	return c * cs + cross(k, c) * sin(a) + k * dot(k, c) * (1.0 - cs);
}

float mask_alpha(vec2 cuv) {
	if (mask_shape < 0) {
		return 1.0;
	}
	vec2 p = (cuv - mask_rect.xy) / max(mask_rect.zw, vec2(0.000001));
	if (mask_shape == 3) {
		// 图片 alpha：和形状共用同一套框，只是 alpha 来自贴图。
		float a = texture(mask_tex, p).a;
		return mask_invert ? 1.0 - a : a;
	}
	float a = shape_alpha(p, mask_shape, mask_radii, mask_feather);
	return mask_invert ? 1.0 - a : a;
}

void fragment() {
	vec4 c = COLOR;
	vec3 rgb = c.rgb;
	rgb += brightness;
	rgb = (rgb - 0.5) * contrast + 0.5;
	float g = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
	rgb = mix(vec3(g), rgb, saturation);
	if (hue != 0.0) {
		rgb = hue_rotate(rgb, hue);
	}
	if (posterize_levels >= 2.0) {
		rgb = floor(clamp(rgb, 0.0, 1.0) * posterize_levels) / posterize_levels;
	}
	// UV 是**合成区域自己的比例坐标**（铺满父级的 SubViewportContainer 的贴图 UV），
	// 不是 SCREEN_UV：嵌套容器/嵌套合成时 SCREEN_UV 会跟着外层 viewport 跑，
	// 遮罩就会错位。遮罩矩形也按同一套 0..1 比例存。
	vec2 cuv = UV;
	float a = mask_alpha(cuv);
	if (vignette_amount > 0.0) {
		vec2 p = (cuv - layer_uv.xy) / max(layer_uv.zw, vec2(0.000001));
		float d = length((p - 0.5) * 2.0);
		float v = smoothstep(vignette_radius, vignette_radius + vignette_softness, d);
		a *= 1.0 - v * vignette_amount;
	}
	vec4 outc = vec4(clamp(rgb, 0.0, 1.0), c.a * clamp(a, 0.0, 1.0));
	// 作用区域：框外显示**原图**（region_blend）或直接裁掉（复制类副本）。
	if (region_shape >= 0) {
		float ra = clamp(region_alpha(cuv), 0.0, 1.0);
		if (region_blend) {
			outc = mix(c, outc, ra);
		} else {
			outc.a *= ra;
		}
	}
	COLOR = outc;
}
"""


## 可分离高斯的一趟（源码）。放在 layer_shader() 之后、BLUR_BODY 之前保持一致，
## 具体内容见文件末尾的常量。
const BLUR_BODY := """
uniform vec2 blur_step = vec2(0.0, 0.0);

void fragment() {
	vec4 acc = vec4(0.0);
	float wsum = 0.0;
	for (int i = -6; i <= 6; i++) {
		float fi = float(i);
		float w = exp(-0.5 * fi * fi / 9.0);
		vec4 s = texture(TEXTURE, UV + blur_step * fi);
		s.rgb *= s.a;
		acc += s * w;
		wsum += w;
	}
	vec4 o = acc / wsum;
	COLOR = vec4(o.a > 0.0001 ? o.rgb / o.a : o.rgb, o.a);
}
"""


## 图层着色器源码。blend 通过 render_mode 表达（同一份源码，只换第一行），
## 这样"混合模式"和"滤镜/遮罩"不会抢同一个 material 槽。
## 两个着色器函数放在源码常量之后：常量在函数体里被引用，顺序清楚更稳妥。
static func layer_shader(blend: String) -> String:
	var mode := "blend_mix"
	match blend:
		"add":
			mode = "blend_add"
		"sub":
			mode = "blend_sub"
		"mul":
			mode = "blend_mul"
	return "shader_type canvas_item;\nrender_mode " + mode + ";\n" + LAYER_BODY


## 可分离高斯的一趟。blur_step 是 UV 方向上的步长（已含半径与方向），
## 半径按图层宽高的比例给，所以窗口放大时模糊的视觉强度不变。
static func blur_shader() -> String:
	return "shader_type canvas_item;\n" + BLUR_BODY


## 作用区域的"混合"着色器：模糊那一套没法在单趟 fragment 里把原图拿回来
## （最后一步拿到的贴图已经是糊的了），所以模糊 + 区域时在整条链外面再包一层，
## 把**滤镜结果**（TEXTURE）和**原图**（base_tex）按区域形状混合。
## base_tex 必须是同一个 viewport 里先渲染好的那张（见 layer_nodes.wrap_content_viewport
## 的注释：跨 viewport 采样只有这种"先渲后取"的顺序才是当帧正确的）。
const REGION_MIX_BODY := """
uniform sampler2D base_tex : filter_linear, repeat_disable;
""" + REGION_BODY + """
void fragment() {
	vec4 f = texture(TEXTURE, UV);
	vec4 b = texture(base_tex, UV);
	// 先在预乘空间混合，否则半透明边缘会渗出黑边（和模糊着色器同一处理）。
	f.rgb *= f.a;
	b.rgb *= b.a;
	vec4 o = mix(b, f, clamp(region_alpha(UV), 0.0, 1.0));
	COLOR = vec4(o.a > 0.0001 ? o.rgb / o.a : o.rgb, o.a);
}
"""


static func region_mix_shader() -> String:
	return "shader_type canvas_item;\n" + REGION_MIX_BODY
