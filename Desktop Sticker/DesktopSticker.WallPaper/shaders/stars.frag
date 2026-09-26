#version 450
// 星空背景：两层网格哈希星星（闪烁）+ 大尺度淡星云，底色与原清理色一致。
// 全屏三角形打底，模型画在它上面（深度仍然从 1 清起）。
layout(location = 0) out vec4 outColor;
layout(push_constant) uniform Push {
    vec4 iTime;        // x = 秒
    vec4 iResolution;  // xy = 像素尺寸
} pc;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

void main() {
    vec2 uv = gl_FragCoord.xy / pc.iResolution.xy;
    vec2 asp = vec2(pc.iResolution.x / pc.iResolution.y, 1.0);
    vec2 pos = uv * asp * 60.0;
    vec3 col = vec3(0.027, 0.043, 0.071);   // #070b12

    // 两层星星：网格哈希决定有没有星、星在格内的位置、亮度与闪烁相位
    for (int layer = 0; layer < 2; ++layer) {
        float scale = (layer == 0) ? 1.0 : 1.9;
        vec2 cell = floor(pos * scale);
        vec2 f = fract(pos * scale);
        float rnd = hash21(cell + float(layer) * 17.0);
        float star = step(0.92 + float(layer) * 0.03, rnd);
        vec2 center = vec2(hash21(cell + 3.7), hash21(cell + 9.1));
        float d = length(f - center);
        float tw = 0.6 + 0.4 * sin(pc.iTime.x * (1.5 + rnd * 2.5) + rnd * 40.0);
        col += vec3(0.8, 0.9, 1.0) * star * smoothstep(0.18, 0.0, d) * tw *
               ((layer == 0) ? 0.9 : 0.5);
    }

    // 淡星云：大尺度正弦起伏，缓慢漂移
    float neb = sin(uv.x * 5.0 + 1.7) * sin(uv.y * 4.0 - 0.6) +
                sin((uv.x + uv.y) * 3.0 + pc.iTime.x * 0.05);
    col += vec3(0.05, 0.09, 0.16) * (0.5 + 0.5 * neb) * 0.35;

    outColor = vec4(col, 1.0);
}
