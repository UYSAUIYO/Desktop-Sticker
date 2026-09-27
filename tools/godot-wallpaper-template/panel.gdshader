shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_back, diffuse_burley, specular_schlick_ggx;

// 倒角面板材质：深色底 + 行波发光的缝 + 菲涅尔轮廓。
// 与原生 Vulkan 场景的 bevel.frag 同一套视觉语言，语言换成 GDShader。

uniform vec4 seam_color : source_color = vec4(0.06, 0.95, 0.82, 1.0);
uniform float wave_speed = 2.6;
uniform float glow_strength : hint_range(0.0, 3.0) = 1.2;

varying vec3 wpos;

void vertex() {
	wpos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	float fres = pow(1.0 - clamp(dot(NORMAL, VIEW), 0.0, 1.0), 3.0);
	float wave = 0.5 + 0.5 * sin(dot(normalize(wpos), normalize(vec3(0.3, 0.9, 0.45))) * 6.5 - TIME * wave_speed);

	ALBEDO = mix(vec3(0.02, 0.10, 0.12), vec3(0.10, 0.90, 0.78), wave * 0.55);
	EMISSION = seam_color.rgb * (0.25 + 0.9 * wave * wave) * glow_strength + fres * vec3(0.10, 0.40, 0.50);
	ROUGHNESS = 0.35;
	METALLIC = 0.2;
}
