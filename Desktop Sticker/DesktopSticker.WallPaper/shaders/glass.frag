#version 450
// 玻璃外壳：菲涅尔驱动的半透明壳，边缘处不透明度上升，正面几乎全透。
layout(location = 0) in vec3 vColor;    // 未使用（壳体颜色由着色器给出）
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vWorldPos;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform Scene {
    mat4 viewProj;
    vec4 lightDir;
    vec4 eyePos;
    vec4 misc;
} u;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 params;    // x = 整体不透明度系数
} p;

void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(u.eyePos.xyz - vWorldPos);
    float f = pow(1.0 - abs(dot(N, V)), 3.0);

    vec3 color = mix(vec3(0.02, 0.07, 0.15), vec3(0.30, 0.85, 0.95), f);
    color += vec3(0.4, 0.7, 0.9) * pow(max(dot(N, normalize(u.lightDir.xyz)), 0.0), 24.0) * 0.35;

    float alpha = (0.05 + 0.6 * f) * p.params.x;
    outColor = vec4(color, alpha);
}
