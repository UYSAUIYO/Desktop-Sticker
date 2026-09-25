#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/wallpaper/MatMath.h>
#include <desktopsticker/wallpaper/Polyhedron.h>

#include <set>

using namespace desktopsticker::wallpaper;

// ---------------- 矩阵 ----------------

TEST(MatMath_IdentityIsNeutral) {
    const Mat4 id = identity();
    const Mat4 r = multiply(id, id);
    for (int i = 0; i < 16; ++i) ASSERT_TRUE(r.m[i] == id.m[i]);
    const Vec3 p = transform_point(id, { 1.0, 2.0, 3.0 });
    ASSERT_TRUE(p.x == 1.0 && p.y == 2.0 && p.z == 3.0);
}

TEST(MatMath_PerspectiveMapsNearToZeroAndFarToOne) {
    // Vulkan 的深度范围是 [0,1]，不是 OpenGL 的 [-1,1] —— 这是移植时最容易错的地方
    const double nearZ = 0.5, farZ = 100.0;
    const Mat4 proj = perspective_vulkan(1.0, 16.0 / 9.0, nearZ, farZ);

    double w = 0.0;
    const Vec3 atNear = transform_point(proj, { 0.0, 0.0, -nearZ }, &w);
    ASSERT_TRUE(w > 0.0);
    ASSERT_TRUE(atNear.z > -1e-6 && atNear.z < 1e-6);

    const Vec3 atFar = transform_point(proj, { 0.0, 0.0, -farZ }, &w);
    ASSERT_TRUE(w > 0.0);
    ASSERT_TRUE(atFar.z > 0.999 && atFar.z < 1.001);
}

TEST(MatMath_PerspectiveFlipsYForVulkanClipSpace) {
    const Mat4 proj = perspective_vulkan(1.0, 1.0, 1.0, 10.0);
    const Vec3 above = transform_point(proj, { 0.0, 1.0, -5.0 });
    ASSERT_TRUE(above.y < 0.0);   // 世界坐标在上，Vulkan 裁剪空间里应在负 y
}

TEST(MatMath_LookAtPutsTargetOnTheNegativeZAxis) {
    const Mat4 view = look_at({ 0.0, 0.0, 5.0 }, { 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 });
    const Vec3 origin = transform_point(view, { 0.0, 0.0, 0.0 });
    ASSERT_TRUE(std::abs(origin.x) < 1e-6);
    ASSERT_TRUE(std::abs(origin.y) < 1e-6);
    ASSERT_TRUE(origin.z < -4.9 && origin.z > -5.1);   // 观察空间里前方是 -Z
}

TEST(MatMath_RotationYIsCounterClockwise) {
    // 右手系下绕 Y 转 +90°：+X 轴转到 -Z
    const Vec3 p = transform_point(rotation_y(3.14159265358979323846 / 2.0), { 1.0, 0.0, 0.0 });
    ASSERT_TRUE(std::abs(p.x) < 1e-6);
    ASSERT_TRUE(std::abs(p.z + 1.0) < 1e-6);
}

TEST(MatMath_RotationXIsCounterClockwise) {
    // 绕 X 转 +90°：+Y 轴转到 +Z
    const Vec3 p = transform_point(rotation_x(3.14159265358979323846 / 2.0), { 0.0, 1.0, 0.0 });
    ASSERT_TRUE(std::abs(p.y) < 1e-6);
    ASSERT_TRUE(std::abs(p.z - 1.0) < 1e-6);
}

TEST(MatMath_ScaleIsUniform) {
    const Vec3 p = transform_point(scale_uniform(3.0), { 1.0, 2.0, 3.0 });
    ASSERT_TRUE(p.x == 3.0 && p.y == 6.0 && p.z == 9.0);
}

// ---------------- 凸包（先用一个独立的简单体验证算法本身） ----------------

TEST(ConvexHull_CubeHasSixQuadFaces) {
    std::vector<Vec3> cube;
    for (double x : { -1.0, 1.0 }) {
        for (double y : { -1.0, 1.0 }) {
            for (double z : { -1.0, 1.0 }) cube.push_back({ x, y, z });
        }
    }
    const auto faces = convex_hull_faces(cube);
    ASSERT_EQ(static_cast<size_t>(6), faces.size());
    for (const auto& f : faces) ASSERT_EQ(static_cast<size_t>(4), f.size());
    // 每条边被两个面共享：面边数之和 = 2E
    size_t edges2 = 0;
    for (const auto& f : faces) edges2 += f.size();
    ASSERT_EQ(static_cast<size_t>(24), edges2);   // E = 12 → 2E = 24
}

TEST(ConvexHull_CoplanarDuplicatesAreNotDropped) {
    // 截角二十面体里相邻的两个六边形共享一条边：按"顶点集合相同"去重会误删真面，
    // 所以这里专测六边形的数量必须是 20（见下一个用例）
    const auto pts = truncated_icosahedron_points(2.0);
    const auto faces = convex_hull_faces(pts);
    int hexagons = 0;
    for (const auto& f : faces) if (f.size() == 6) hexagons++;
    ASSERT_EQ(20, hexagons);
}

// ---------------- 截角二十面体 ----------------

TEST(Polyhedron_HasSixtyVerticesOnTheGivenRadius) {
    const auto pts = truncated_icosahedron_points(2.0);
    ASSERT_EQ(static_cast<size_t>(60), pts.size());
    for (const auto& p : pts) {
        ASSERT_TRUE(std::abs(length(p) - 2.0) < 1e-9);
    }
}

TEST(Polyhedron_Has32FacesWith12PentagonsAnd20Hexagons) {
    const auto pts = truncated_icosahedron_points(2.0);
    const auto faces = convex_hull_faces(pts);
    ASSERT_EQ(static_cast<size_t>(32), faces.size());

    int pentagons = 0;
    int hexagons = 0;
    for (const auto& f : faces) {
        if (f.size() == 5) pentagons++;
        else if (f.size() == 6) hexagons++;
    }
    ASSERT_EQ(12, pentagons);
    ASSERT_EQ(20, hexagons);
}

TEST(Polyhedron_SatisfiesEulerFormulaAndEdgeSharing) {
    // V - E + F = 2；每条边恰好被两个面共享 → 面边数之和 = 2E
    const auto pts = truncated_icosahedron_points(2.0);
    const auto faces = convex_hull_faces(pts);

    size_t edgeSlots = 0;
    for (const auto& f : faces) edgeSlots += f.size();
    ASSERT_EQ(static_cast<size_t>(0), edgeSlots % 2);
    const size_t edges = edgeSlots / 2;
    ASSERT_EQ(static_cast<size_t>(90), edges);
    const long long euler = static_cast<long long>(pts.size()) -
                            static_cast<long long>(edges) +
                            static_cast<long long>(faces.size());
    ASSERT_EQ(2LL, euler);
}

TEST(Polyhedron_EveryFaceIsPlanarAndFacesOutward) {
    const auto pts = truncated_icosahedron_points(2.0);
    const auto faces = convex_hull_faces(pts);

    for (const auto& f : faces) {
        // 面法线取环上相邻两条边
        const Vec3 e1 = pts[static_cast<size_t>(f[1])] - pts[static_cast<size_t>(f[0])];
        const Vec3 e2 = pts[static_cast<size_t>(f[2])] - pts[static_cast<size_t>(f[1])];
        const Vec3 normal = normalize(cross(e1, e2));

        Vec3 centroid{};
        for (int idx : f) centroid = centroid + pts[static_cast<size_t>(idx)];
        centroid = centroid * (1.0 / static_cast<double>(f.size()));

        // 面朝外：法线与"从原点指向面心"同向
        ASSERT_TRUE(dot(normal, centroid) > 0.0);
        // 共面：所有顶点到该平面的距离约为 0
        const double d = dot(normal, pts[static_cast<size_t>(f[0])]);
        for (int idx : f) {
            ASSERT_TRUE(std::abs(dot(normal, pts[static_cast<size_t>(idx)]) - d) < 1e-9);
        }
    }
}

TEST(Polyhedron_MeshIsTriangulatedPerFaceWithFaceIds) {
    const std::vector<Vec3> palette = {
        { 0.23, 0.51, 0.96 }, { 0.13, 0.83, 0.93 }, { 0.39, 0.40, 0.95 },
        { 0.05, 0.65, 0.91 }, { 0.55, 0.36, 0.96 }, { 0.08, 0.72, 0.65 },
    };
    const MeshData mesh = build_truncated_icosahedron(palette, 2.0);

    ASSERT_EQ(32, mesh.faceCount);
    // 五边形 3 个三角形 + 六边形 4 个 = 12*3 + 20*4 = 116 个三角形
    ASSERT_EQ(static_cast<size_t>(116 * 3), mesh.vertices.size());
    ASSERT_EQ(mesh.vertices.size() / 3, mesh.faceIds.size());

    // 同一个面的三个顶点必须同色、同法线
    for (size_t t = 0; t < mesh.faceIds.size(); ++t) {
        const MeshVertex& a = mesh.vertices[t * 3 + 0];
        const MeshVertex& b = mesh.vertices[t * 3 + 1];
        const MeshVertex& c = mesh.vertices[t * 3 + 2];
        ASSERT_TRUE(a.color.x == b.color.x && b.color.x == c.color.x);
        ASSERT_TRUE(a.normal.x == b.normal.x && a.normal.y == b.normal.y);
    }

    // 每个面编号都出现，且 32 个面都用到了调色板里的颜色
    std::set<uint32_t> ids(mesh.faceIds.begin(), mesh.faceIds.end());
    ASSERT_EQ(static_cast<size_t>(32), ids.size());
    std::set<double> usedColors;
    for (const auto& v : mesh.vertices) usedColors.insert(v.color.x);
    ASSERT_TRUE(usedColors.size() >= 6);
}

TEST(Polyhedron_EmptyPaletteFallsBackToASingleColor) {
    const MeshData mesh = build_truncated_icosahedron({}, 2.0);
    ASSERT_EQ(32, mesh.faceCount);
    for (const auto& v : mesh.vertices) {
        ASSERT_TRUE(v.color.x == 0.6 && v.color.y == 0.7 && v.color.z == 0.9);
    }
}
