/**************************************************************************/
/*  wallpaper_effects_builtin.h  —  Desktop Sticker fork                   */
/*  内置壁纸特效清单：每个特效 = 名称 + 分类 + canvas_item 着色器源码。      */
/*  着色器源码保持纯 ASCII（走 String(const char*) 的 latin1 解码无碍）；   */
/*  名称/分类是 UTF-8，使用时用 String::utf8() 包一层。                     */
/**************************************************************************/

#pragma once

struct WallpaperEffect {
	const char *name;     // UTF-8 中文名
	const char *category; // UTF-8 分类
	const char *code;     // canvas_item 着色器源码（ASCII）
};

// clang-format off
static const WallpaperEffect kWpEffects[] = {
	{ "水波", "动画", R"WPS(
shader_type canvas_item;
// Water ripple modeled on Wallpaper Engine's waterripple: two scrolling
// analytic normals summed + specular + optional mask texture.
// (WE samples a baked normal map; here we approximate procedurally so no
// external texture/asset is required. Swap in a normal map for pixel parity.)
uniform float strength : hint_range(0.0, 0.2) = 0.04;
uniform float scale    : hint_range(1.0, 20.0) = 8.0;
uniform float speed    : hint_range(0.0, 1.0) = 0.2;
uniform float specular : hint_range(0.0, 1.0) = 0.5;
uniform sampler2D mask : filter_linear;
uniform bool use_mask = false;

vec3 ripple_normal(vec2 uv, float t) {
	vec2 g;
	g.x = cos(uv.x * 6.2831 + t) * 3.1416 + cos(uv.y * 4.0 - t * 0.7) * 2.0;
	g.y = cos(uv.y * 5.137 - t * 1.3) * 2.568 + cos(uv.x * 3.3 + t * 0.5) * 1.65;
	return normalize(vec3(-g * 0.02, 1.0));
}

void fragment() {
	float m = 1.0;
	if (use_mask) {
		m = clamp(texture(mask, UV).r, 0.0, 1.0);
	}
	float t = TIME * speed;
	vec3 n1 = ripple_normal(UV * scale, t);
	vec3 n2 = ripple_normal(UV * scale * 1.37 + vec2(4.3, 1.7), t * 1.13);
	vec3 n = normalize(n1 + n2);
	COLOR = texture(TEXTURE, UV + n.xy * strength * m);
	vec2 lightdir = normalize(vec2(0.5, -0.8));
	float s = pow(max(dot(n.xy, lightdir), 0.0), 20.0) * specular * m;
	COLOR.rgb += vec3(s);
}
)WPS" },

	{ "雨", "动画", R"WPS(
shader_type canvas_item;
uniform float density = 200.0;
uniform float speed = 25.0;
uniform vec2 wind = vec2(0.15, 1.0);
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
void fragment() {
	vec4 base = texture(TEXTURE, UV);
	vec2 uv = UV * vec2(density, density * 0.5);
	uv += TIME * speed * normalize(wind);
	vec2 cell = floor(uv);
	float x = hash(cell);
	float t = fract(uv.y * 0.5 + x * 10.0);
	float streak = smoothstep(0.0, 0.15, t) * smoothstep(0.5, 0.15, t);
	float lane = step(0.9, x);
	vec3 rain = vec3(0.75, 0.82, 0.9) * streak * lane;
	COLOR = vec4(base.rgb + rain, base.a);
}
)WPS" },

	{ "模糊", "模糊", R"WPS(
shader_type canvas_item;
uniform float radius = 3.0;
void fragment() {
	vec2 px = PIXEL_SIZE * radius;
	vec4 c = vec4(0.0);
	c += texture(TEXTURE, UV + vec2(-px.x, -px.y));
	c += texture(TEXTURE, UV + vec2(0.0, -px.y));
	c += texture(TEXTURE, UV + vec2(px.x, -px.y));
	c += texture(TEXTURE, UV + vec2(-px.x, 0.0));
	c += texture(TEXTURE, UV);
	c += texture(TEXTURE, UV + vec2(px.x, 0.0));
	c += texture(TEXTURE, UV + vec2(-px.x, px.y));
	c += texture(TEXTURE, UV + vec2(0.0, px.y));
	c += texture(TEXTURE, UV + vec2(px.x, px.y));
	COLOR = c / 9.0;
}
)WPS" },

	{ "扭曲", "动画", R"WPS(
shader_type canvas_item;
uniform float strength = 1.0;
uniform float speed = 0.5;
void fragment() {
	vec2 c = UV - 0.5;
	float r = length(c);
	float a = atan(c.y, c.x) + strength * (1.0 - r) * (0.5 + 0.5 * sin(TIME * speed));
	vec2 uv = vec2(cos(a), sin(a)) * r + 0.5;
	COLOR = texture(TEXTURE, uv);
}
)WPS" },

	{ "旋转", "动画", R"WPS(
shader_type canvas_item;
uniform float speed = 0.3;
void fragment() {
	vec2 c = UV - 0.5;
	float ang = TIME * speed;
	mat2 rot = mat2(cos(ang), -sin(ang), sin(ang), cos(ang));
	vec2 uv = rot * c + 0.5;
	if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
		COLOR = vec4(0.0);
	} else {
		COLOR = texture(TEXTURE, uv);
	}
}
)WPS" },

	{ "涟漪", "互动", R"WPS(
shader_type canvas_item;
uniform float amplitude = 0.03;
uniform float frequency = 30.0;
uniform float speed = 3.0;
void fragment() {
	vec2 c = UV - 0.5;
	float r = length(c);
	vec2 dir = r > 0.0001 ? c / r : vec2(0.0);
	vec2 uv = UV + dir * sin(r * frequency - TIME * speed) * amplitude;
	COLOR = texture(TEXTURE, uv);
}
)WPS" },
};
// clang-format on

static const int kWpEffectCount = static_cast<int>(sizeof(kWpEffects) / sizeof(kWpEffects[0]));
