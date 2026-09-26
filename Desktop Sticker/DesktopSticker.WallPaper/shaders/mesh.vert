#version 450
// 内置 3D 模型的顶点着色器。模型矩阵走 push_constant：同一帧里实体/玻璃壳/倒影/
// 线框笼要用不同的模型变换，uniform 只放所有阶段共享的部分。
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

layout(location = 0) out vec3 vColor;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vWorldPos;

layout(set = 0, binding = 0) uniform Scene {
    mat4 viewProj;
    vec4 lightDir;   // xyz = 指向光源的方向（世界空间，已归一化）
    vec4 eyePos;     // xyz = 相机位置
    vec4 misc;       // x = 秒
} u;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 params;     // x = 不透明度，y = 缝隙发光强度
} p;

void main() {
    vec4 world = p.model * vec4(inPosition, 1.0);
    vWorldPos = world.xyz;
    // 本场景的模型矩阵只有旋转与均匀缩放，直接取 mat3 变换法线即可
    vNormal = normalize(mat3(p.model) * inNormal);
    vColor = inColor;
    gl_Position = u.viewProj * world;
}
