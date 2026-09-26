#version 450
// 倒角实体的片元着色器：面板走 Blinn-Phong + 菲涅尔边缘光；倒角条/顶帽（顶点亮度低）
// 叠加沿球面行波的缝隙发光。params.x < 1 时是倒影通道，随沉入深度渐隐。
layout(location = 0) in vec3 vColor;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vWorldPos;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform Scene {
    mat4 viewProj;
    vec4 lightDir;
    vec4 eyePos;
    vec4 misc;      // x = 秒
} u;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 params;    // x = 不透明度，y = 缝隙发光强度
} p;

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(u.lightDir.xyz);
    vec3 V = normalize(u.eyePos.xyz - vWorldPos);
    vec3 H = normalize(L + V);

    float diffuse  = max(dot(N, L), 0.0);
    float specular = pow(max(dot(N, H), 0.0), 64.0) * 0.45;
    vec3 color = vColor * (0.16 + 0.9 * diffuse) + vec3(specular);

    // 菲涅尔边缘光：视线掠射处提亮，把球体从背景里剥出来
    float fres = pow(1.0 - max(dot(N, V), 0.0), 3.0);
    color += vec3(0.10, 0.34, 0.44) * fres * 0.8;

    // 缝隙行波：倒角条/顶帽的顶点亮度接近 0，行波按世界位置相位推进
    float lum  = dot(vColor, vec3(0.299, 0.587, 0.114));
    float seam = 1.0 - smoothstep(0.0, 0.12, lum);
    float wave = 0.5 + 0.5 * sin(dot(normalize(vWorldPos + 1e-4),
                                     normalize(vec3(0.3, 0.9, 0.45))) * 6.5 - u.misc.x * 2.6);
    color += vec3(0.10, 0.90, 0.78) * seam * (0.18 + 0.9 * wave * wave) * p.params.y;

    // 倒影通道（params.x < 1）：世界 y 越沉越淡
    float fade = mix(1.0, clamp(1.0 + vWorldPos.y * 0.30, 0.0, 1.0),
                     step(p.params.x, 0.999));
    outColor = vec4(pow(color * fade, vec3(1.0 / 1.1)), p.params.x);
}
