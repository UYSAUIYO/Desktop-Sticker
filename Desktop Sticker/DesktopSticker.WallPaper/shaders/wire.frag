#version 450
// 外层线框笼：纯自发光的线段，缓慢脉动。
layout(location = 0) in vec3 vColor;
layout(location = 1) in vec3 vNormal;   // 线段不使用
layout(location = 2) in vec3 vWorldPos; // 线段不使用

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform Scene {
    mat4 viewProj;
    vec4 lightDir;
    vec4 eyePos;
    vec4 misc;
} u;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 params;    // x = 不透明度
} p;

void main() {
    float pulse = 1.1 + 0.35 * sin(u.misc.x * 1.9);
    outColor = vec4(vColor * pulse, 0.85 * p.params.x);
}
