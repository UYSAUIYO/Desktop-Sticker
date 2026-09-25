#pragma once

// 内置 3D 模型：截角二十面体（"足球形"，12 个五边形 + 20 个六边形 = 32 面）。
//
// 顶点构造与 Three.js 参考实现一致（把 icosahedron 的顶点做循环置换并取正负号）：
//   (0, ±1, ±3φ)            12 个
//   (±1, ±(2+φ), ±2φ)       24 个
//   (±φ, ±2, ±(2φ+1))       24 个
// 合计 60 个顶点。参考实现用 ConvexGeometry 求凸包；这里自己写一个通用凸包，
// 因为它是纯函数、可以单测（面的数量、每个面的边数、Euler 数、外向法线都能验），
// 而把 Three.js 的行为搬到 C++ 里恰恰是最容易悄悄出错的一步。
//
// 输出：三角形列表（每面扇形三角化）+ 每个三角形所属的面编号（用来按面上色）。

#include <algorithm>
#include <cstdint>
#include <vector>

#include "MatMath.h"

namespace desktopsticker::wallpaper {

// 与 GLSL 顶点输入一一对应
struct MeshVertex {
    Vec3 position;
    Vec3 normal;
    Vec3 color;
};

struct MeshData {
    std::vector<MeshVertex> vertices;   // 三角形列表（未索引化：面法线是硬边，本来就要拆点）
    std::vector<uint32_t> faceIds;      // 与 vertices 同步，每 3 个一组是同一个面
    int faceCount = 0;
};

namespace detail {

inline bool same_plane(const Vec3& n1, double d1, const Vec3& n2, double d2, double eps) {
    // 法线已归一化；同向且距离相同才算同一个平面
    return dot(n1, n2) > 1.0 - eps && std::abs(d1 - d2) < eps * 4.0;
}

} // namespace detail

// 凸包：返回每个面按逆时针（从外面看）排好序的顶点环。
// 做法是 O(n^3) 枚举三点定平面，再验证其余点都在该平面同侧 —— n=60 时约 3.4 万次，
// 启动时一次性计算，毫秒级，换来的是"不需要任何外部几何库"。
inline std::vector<std::vector<int>> convex_hull_faces(const std::vector<Vec3>& pts, double eps = 1e-6) {
    std::vector<std::vector<int>> faces;
    std::vector<Vec3> faceNormals;      // 与 faces 同步，去重时比平面而不是比顶点集合
    std::vector<double> faceOffsets;
    if (pts.size() < 4) return faces;

    const size_t n = pts.size();
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            for (size_t k = j + 1; k < n; ++k) {
                const Vec3 raw = cross(pts[j] - pts[i], pts[k] - pts[i]);
                const double len = length(raw);
                if (len < eps) continue;                 // 三点共线
                const Vec3 normal = raw * (1.0 / len);
                const double d = dot(normal, pts[i]);

                // 所有点都必须在法线负侧（法线朝外）：有点在正侧就说明这不是支撑面。
                // 同时要求"确实有点在负侧"，否则三点共面退化。
                bool positive = false;
                bool anyBehind = false;
                for (size_t t = 0; t < n; ++t) {
                    const double dist = dot(normal, pts[t]) - d;
                    if (dist > eps) positive = true;
                    if (dist < -eps) anyBehind = true;
                }
                if (positive || !anyBehind) continue;

                // 去重按**平面**判定：同一平面会被不同的三点反复命中。
                // 不能按"顶点集合相同"判 —— 相邻的两个六边形共享一条边，那样会误删真面。
                bool duplicate = false;
                for (size_t e = 0; e < faces.size(); ++e) {
                    if (detail::same_plane(faceNormals[e], faceOffsets[e], normal, d, eps)) {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate) continue;

                // 收集落在该平面上的点
                std::vector<int> ring;
                for (size_t t = 0; t < n; ++t) {
                    if (std::abs(dot(normal, pts[t]) - d) <= eps * 4.0) {
                        ring.push_back(static_cast<int>(t));
                    }
                }
                if (ring.size() < 3) continue;

                // 在平面内按极角排序，得到多边形环
                Vec3 centroid{};
                for (int idx : ring) centroid = centroid + pts[static_cast<size_t>(idx)];
                centroid = centroid * (1.0 / static_cast<double>(ring.size()));

                const Vec3 axisU = normalize(pts[static_cast<size_t>(ring[0])] - centroid);
                const Vec3 axisV = cross(normal, axisU);
                std::sort(ring.begin(), ring.end(), [&](int a, int b) {
                    const Vec3 va = pts[static_cast<size_t>(a)] - centroid;
                    const Vec3 vb = pts[static_cast<size_t>(b)] - centroid;
                    const double angA = std::atan2(dot(va, axisV), dot(va, axisU));
                    const double angB = std::atan2(dot(vb, axisV), dot(vb, axisU));
                    return angA < angB;
                });

                // 定朝向：normal 朝外时，从外面看环必须是逆时针
                const Vec3 e1 = pts[static_cast<size_t>(ring[1])] - pts[static_cast<size_t>(ring[0])];
                const Vec3 e2 = pts[static_cast<size_t>(ring[2])] - pts[static_cast<size_t>(ring[1])];
                if (dot(cross(e1, e2), normal) < 0.0) std::reverse(ring.begin(), ring.end());

                faces.push_back(ring);
                faceNormals.push_back(normal);
                faceOffsets.push_back(d);
            }
        }
    }
    return faces;
}

// 生成截角二十面体的顶点（已居中并归一化到半径 radius）
inline std::vector<Vec3> truncated_icosahedron_points(double radius = 2.0) {
    const double phi = (1.0 + std::sqrt(5.0)) * 0.5;
    const double signs[2] = { 1.0, -1.0 };

    auto push_cyclic = [](std::vector<Vec3>& out, double a, double b, double c) {
        out.push_back({ a, b, c });
        out.push_back({ b, c, a });
        out.push_back({ c, a, b });
    };

    std::vector<Vec3> pts;
    pts.reserve(60);
    for (double s1 : signs) {
        for (double s2 : signs) push_cyclic(pts, 0.0, s1 * 1.0, s2 * 3.0 * phi);
    }
    for (double s1 : signs) {
        for (double s2 : signs) {
            for (double s3 : signs) {
                push_cyclic(pts, s1 * 1.0, s2 * (2.0 + phi), s3 * 2.0 * phi);
            }
        }
    }
    for (double s1 : signs) {
        for (double s2 : signs) {
            for (double s3 : signs) {
                push_cyclic(pts, s1 * phi, s2 * 2.0, s3 * (2.0 * phi + 1.0));
            }
        }
    }

    // 居中 + 缩放到指定半径（参考实现同样先 center 再按包围球归一化）
    Vec3 centroid{};
    for (const auto& p : pts) centroid = centroid + p;
    centroid = centroid * (1.0 / static_cast<double>(pts.size()));

    double maxDist = 0.0;
    for (auto& p : pts) {
        p = p - centroid;
        // 手写比较而不是 std::max：这是头文件，会被不定义 NOMINMAX 的工程包含，
        // windows.h 的 max 宏会把 std::max 打散（Tests 工程就是这么炸的）
        const double dist = length(p);
        if (dist > maxDist) maxDist = dist;
    }
    const double k = maxDist > 0.0 ? radius / maxDist : 1.0;
    for (auto& p : pts) p = p * k;
    return pts;
}

// 组装可直接上传的三角形网格。palette 按面的出现顺序循环取色，
// 与参考实现 `palette[idx++ % palette.length]` 的分面着色一致。
inline MeshData build_truncated_icosahedron(const std::vector<Vec3>& palette, double radius = 2.0) {
    const std::vector<Vec3> pts = truncated_icosahedron_points(radius);
    const std::vector<std::vector<int>> faces = convex_hull_faces(pts);

    MeshData mesh;
    mesh.faceCount = static_cast<int>(faces.size());
    const Vec3 fallbackColor{ 0.6, 0.7, 0.9 };

    for (size_t f = 0; f < faces.size(); ++f) {
        const auto& ring = faces[f];
        if (ring.size() < 3) continue;

        // 面法线：环的朝向已经保证朝外
        const Vec3 centroid = [&] {
            Vec3 c{};
            for (int idx : ring) c = c + pts[static_cast<size_t>(idx)];
            return c * (1.0 / static_cast<double>(ring.size()));
        }();
        const Vec3 normal = normalize(cross(pts[static_cast<size_t>(ring[1])] - pts[static_cast<size_t>(ring[0])],
                                           pts[static_cast<size_t>(ring[2])] - pts[static_cast<size_t>(ring[1])]));
        const Vec3 color = palette.empty() ? fallbackColor
                                           : palette[f % palette.size()];
        (void)centroid;

        // 扇形三角化：凸多边形，任选一个顶点即可
        for (size_t t = 1; t + 1 < ring.size(); ++t) {
            const int tri[3] = { ring[0], ring[t], ring[t + 1] };
            for (int v : tri) {
                MeshVertex mv;
                mv.position = pts[static_cast<size_t>(v)];
                mv.normal = normal;
                mv.color = color;
                mesh.vertices.push_back(mv);
            }
            mesh.faceIds.push_back(static_cast<uint32_t>(f));
        }
    }
    return mesh;
}

} // namespace desktopsticker::wallpaper
