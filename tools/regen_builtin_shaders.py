# 内置着色器 SPIR-V 头文件再生成：
#   1) 先用 glslangValidator 编译 shaders/ 下的 GLSL 为同名 .spv（见 AGENTS.md 命令）；
#   2) 再运行本脚本：  python tools/regen_builtin_shaders.py
# 它读取 DesktopSticker.WallPaper/shaders/*.spv，整体重写 src/VulkanBuiltinShaders.h。
# 本脚本不做任何进程调用，编译由调用方负责。
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SHADER_DIR = ROOT / "Desktop Sticker" / "DesktopSticker.WallPaper" / "shaders"
OUT_HEADER = ROOT / "Desktop Sticker" / "DesktopSticker.WallPaper" / "src" / "VulkanBuiltinShaders.h"

# (数组名, 源文件名 = 同名 .spv)
SHADERS = [
    ("kFullscreenVertSpv", "fullscreen.vert"),
    ("kDefaultFragSpv", "default.frag"),
    ("kMeshVertSpv", "mesh.vert"),
    ("kBevelFragSpv", "bevel.frag"),
    ("kGlassFragSpv", "glass.frag"),
    ("kWireFragSpv", "wire.frag"),
    ("kStarsFragSpv", "stars.frag"),
]


def read_words(src: str) -> list[int]:
    spv = SHADER_DIR / (src + ".spv")
    data = spv.read_bytes()
    if len(data) % 4 != 0:
        raise SystemExit(f"{src}: spv size not multiple of 4")
    return list(struct.unpack(f"<{len(data) // 4}I", data))


def emit_array(name: str, words: list[int]) -> str:
    lines = [f"inline constexpr uint32_t {name}[] = {{"]
    for i in range(0, len(words), 6):
        chunk = ", ".join(f"0x{w:08X}u" for w in words[i:i + 6])
        lines.append(f"    {chunk},")
    lines.append("};")
    return "\n".join(lines)


def main() -> None:
    arrays = [(name, read_words(src)) for name, src in SHADERS]

    parts = [
        "#pragma once",
        "",
        "// 内置着色器的 SPIR-V（④）。GLSL 源文件在 DesktopSticker.WallPaper/shaders/，",
        "// 改动着色器后先用 glslangValidator 编译出同名 .spv，再运行",
        "// tools/regen_builtin_shaders.py 重新生成本文件（glslangValidator 由",
        "// tools/prepare_shaderc.ps1 构建）。全屏等离子两支是早期内置内容，",
        "// GLSL 内联在下方注释里。",
        "",
        "// ---- fullscreen.vert（全屏三角形，不用顶点缓冲）----",
        "// #version 450",
        "// void main() {",
        "//     vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);",
        "//     gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);",
        "// }",
        "",
        "// ---- default.frag（默认全屏等离子）----",
        "// #version 450",
        "// layout(location = 0) out vec4 outColor;",
        "// layout(push_constant) uniform Push {",
        "//     vec4 iTime;        // x = 秒",
        "//     vec4 iResolution;  // xy = 像素尺寸",
        "// } pc;",
        "// void main() {",
        "//     vec2 uv = gl_FragCoord.xy / pc.iResolution.xy;",
        "//     float t = pc.iTime.x;",
        "//     float v = sin(uv.x * 8.0 + t) + sin(uv.y * 10.0 - t * 1.3)",
        "//             + sin((uv.x + uv.y) * 6.0 + t * 0.7);",
        "//     vec3 c = 0.5 + 0.5 * cos(vec3(0.0, 2.1, 4.2) + v + t);",
        "//     outColor = vec4(c, 1.0);",
        "// }",
        "",
        "#include <cstdint>",
        "",
        "namespace desktopsticker::wallpaper {",
        "",
    ]
    for name, words in arrays:
        parts.append(emit_array(name, words))
        parts.append("")

    parts += [
        "// push constant 布局，必须与全屏着色器里 GLSL 的 Push 块逐字段一致",
        "struct ShaderPushConstants {",
        "    float iTime[4];         // x = 秒",
        "    float iResolution[4];   // xy = 像素尺寸",
        "};",
        "static_assert(sizeof(ShaderPushConstants) == 32, \"push constant 布局必须与 GLSL 一致\");",
        "",
        "// 模型场景的 uniform 布局，必须与 mesh.vert 等的 Scene 块逐字段一致",
        "struct SceneUniforms {",
        "    float viewProj[16];     // 列主序",
        "    float lightDir[4];      // xyz = 指向光源的方向",
        "    float eyePos[4];        // xyz = 相机位置",
        "    float misc[4];          // x = 秒",
        "};",
        "static_assert(sizeof(SceneUniforms) == 112, \"uniform 布局必须与 GLSL 的 Scene 块一致\");",
        "",
        "// 模型侧 push constant：实体/玻璃壳/倒影/线框笼各用不同的模型矩阵与参数",
        "struct ModelPush {",
        "    float model[16];        // 列主序",
        "    float params[4];        // x = 不透明度，y = 缝隙发光强度",
        "};",
        "static_assert(sizeof(ModelPush) == 80, \"模型 push constant 布局必须与 GLSL 一致\");",
        "",
        "} // namespace desktopsticker::wallpaper",
        "",
    ]
    with open(OUT_HEADER, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(parts))
    print(f"written: {OUT_HEADER}")


if __name__ == "__main__":
    main()
