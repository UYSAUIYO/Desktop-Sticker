#version 450
layout(location = 0) out vec4 outColor;
layout(push_constant) uniform Push {
    vec4 iTime;
    vec4 iResolution;
} pc;
void main() {
    vec2 uv = gl_FragCoord.xy / pc.iResolution.xy;
    float t = pc.iTime.x;
    float v = sin(uv.x * 8.0 + t) + sin(uv.y * 10.0 - t * 1.3)
            + sin((uv.x + uv.y) * 6.0 + t * 0.7);
    vec3 c = 0.5 + 0.5 * cos(vec3(0.0, 2.1, 4.2) + v + t);
    outColor = vec4(c, 1.0);
}
