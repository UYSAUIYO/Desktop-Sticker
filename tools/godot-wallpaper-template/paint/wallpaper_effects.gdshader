shader_type canvas_item;

// 多效果遮罩着色器：mask 的 R/G/B 三个通道分别是三种效果的"覆盖度"。
// 由画笔工具（paint/paint_tool.tscn）刷出来并保存为 wallpaper_mask.png，
// 壁纸主场景的 Sprite2D 引用它即可。
uniform sampler2D mask : filter_linear;
uniform float strength = 1.0;

// —— 效果 A（mask.R）：水波扭曲 ——
uniform float wave_amp = 0.02;
uniform float wave_freq = 14.0;
uniform float wave_speed = 2.0;
// —— 效果 B（mask.G）：去色（灰度）——
// —— 效果 C（mask.B）：冷色辉光 ——
uniform float glow_gain = 0.8;

void fragment() {
	vec4 base = texture(TEXTURE, UV);
	vec3 m = clamp(texture(mask, UV).rgb, 0.0, 1.0) * strength;
	vec4 col = base;

	// A：水波 —— 用覆盖度调制 UV 扰动幅度
	if (m.r > 0.001) {
		vec2 uv = UV;
		uv.y += sin(uv.x * wave_freq + TIME * wave_speed) * wave_amp * m.r;
		uv.x += cos(uv.y * wave_freq + TIME * wave_speed) * wave_amp * m.r * 0.5;
		col = mix(col, texture(TEXTURE, uv), m.r);
	}
	// B：去色
	if (m.g > 0.001) {
		float g = dot(col.rgb, vec3(0.299, 0.587, 0.114));
		col.rgb = mix(col.rgb, vec3(g), m.g);
	}
	// C：冷色辉光（提亮 + 偏蓝）
	if (m.b > 0.001) {
		vec3 glow = col.rgb * (1.0 + glow_gain) + vec3(0.04, 0.08, 0.14) * glow_gain;
		col.rgb = mix(col.rgb, glow, m.b);
	}

	COLOR = col;
}
