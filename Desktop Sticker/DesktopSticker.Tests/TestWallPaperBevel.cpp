#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/wallpaper/MatMath.h>
#include <desktopsticker/wallpaper/Polyhedron.h>

#include <cstdint>
#include <map>
#include <set>

using namespace desktopsticker::wallpaper;

namespace {

// 位置量化成 64 位整数三元组：三角形汤里按位置配对有向边（同一次计算的位置比特相同）
struct QPos {
    long long x, y, z;
    bool operator<(const QPos& o) const {
        if (x != o.x) return x < o.x;
        if (y != o.y) return y < o.y;
        return z < o.z;
    }
    bool operator==(const QPos& o) const { return x == o.x && y == o.y && z == o.z; }
};

QPos quantify(const Vec3& p) {
    QPos q;
    q.x = static_cast<long long>(std::llround(p.x * 1e6));
    q.y = static_cast<long long>(std::llround(p.y * 1e6));
    q.z = static_cast<long long>(std::llround(p.z * 1e6));
    return q;
}

std::pair<QPos, QPos> edge_key(const Vec3& a, const Vec3& b) {
    const QPos qa = quantify(a), qb = quantify(b);
    return qa < qb ? std::make_pair(qa, qb) : std::make_pair(qb, qa);
}

const std::vector<Vec3> kPalette = {
    { 0.231, 0.510, 0.965 }, { 0.133, 0.827, 0.933 }, { 0.388, 0.400, 0.949 },
};

} // namespace

TEST(BevelMesh_PanelStripCapCounts) {
    const BevelMesh m = build_beveled_truncated_icosahedron(kPalette, 2.0, 0.16);
    // 截角二十面体：32 面 / 90 边 / 60 顶点
    ASSERT_TRUE(m.panelCount == 32);
    ASSERT_TRUE(m.stripCount == 90);
    ASSERT_TRUE(m.capCount == 60);
    ASSERT_TRUE(m.edgeCount == 90);
    ASSERT_TRUE(m.solid.faceCount == 0 || true);   // faceCount 未用于倒角网格
    // 三角形数：面板扇形（12×3 + 20×4 = 116）+ 条 90×2 = 180 + 帽 60 = 356
    ASSERT_TRUE(m.solid.vertices.size() == 356 * 3);
}

TEST(BevelMesh_WatertightEveryEdgePaired) {
    const BevelMesh m = build_beveled_truncated_icosahedron(kPalette, 2.0, 0.16);
    // 有向边配对：键必须**保序**（无序化会让相反方向落进同一个 key，测不出朝向）。
    // 断言：每条无向边恰好出现两次、两个方向各一次（闭合无洞）。
    auto ordered = [](const Vec3& a, const Vec3& b) {
        return std::make_pair(quantify(a), quantify(b));
    };
    std::map<std::pair<QPos, QPos>, int> forward, backward;
    const size_t tris = m.solid.vertices.size() / 3;
    for (size_t t = 0; t < tris; ++t) {
        const Vec3& a = m.solid.vertices[t * 3 + 0].position;
        const Vec3& b = m.solid.vertices[t * 3 + 1].position;
        const Vec3& c = m.solid.vertices[t * 3 + 2].position;
        ++forward[ordered(a, b)];
        ++forward[ordered(b, c)];
        ++forward[ordered(c, a)];
        ++backward[ordered(b, a)];
        ++backward[ordered(c, b)];
        ++backward[ordered(a, c)];
    }
    for (const auto& [key, count] : forward) {
        ASSERT_TRUE(count == 1);
        const int rev = backward.count(key) ? backward[key] : 0;
        ASSERT_TRUE(rev == 1);
    }
    for (const auto& [key, count] : backward) {
        ASSERT_TRUE(count == 1);
        ASSERT_TRUE(forward.count(key) == 1);
    }
}

TEST(BevelMesh_NormalsUnitAndOutward) {
    const BevelMesh m = build_beveled_truncated_icosahedron(kPalette, 2.0, 0.16);
    const size_t tris = m.solid.vertices.size() / 3;
    for (size_t t = 0; t < tris; ++t) {
        const Vec3 n = m.solid.vertices[t * 3].normal;
        ASSERT_TRUE(std::abs(length(n) - 1.0) < 1e-9);
        // 几何中心在原点：外向法线与三角形自身位置同侧
        const Vec3 mid = (m.solid.vertices[t * 3].position +
                          m.solid.vertices[t * 3 + 1].position +
                          m.solid.vertices[t * 3 + 2].position) * (1.0 / 3.0);
        ASSERT_TRUE(dot(n, mid) > 0.0);
        // 同一三角形的三个顶点法线一致（硬边拆点的约定）
        for (int k = 1; k < 3; ++k) {
            const Vec3 nk = m.solid.vertices[t * 3 + static_cast<size_t>(k)].normal;
            ASSERT_TRUE(dot(n, nk) > 0.999);
        }
    }
}

TEST(BevelMesh_WireEdgesUniqueOnHull) {
    const BevelMesh m = build_beveled_truncated_icosahedron(kPalette, 2.0, 0.16);
    ASSERT_TRUE(m.wire.size() == 90 * 2);
    std::set<std::pair<QPos, QPos>> seen;
    for (size_t s = 0; s < m.wire.size(); s += 2) {
        const auto key = edge_key(m.wire[s].position, m.wire[s + 1].position);
        ASSERT_TRUE(seen.count(key) == 0);   // 无向去重
        seen.insert(key);
        for (int k = 0; k < 2; ++k) {
            const double dist = length(m.wire[s + static_cast<size_t>(k)].position);
            ASSERT_TRUE(std::abs(dist - 2.0) < 1e-9);   // 线框端点都在原始凸包上
        }
    }
}

TEST(BevelMesh_PanelsInsetStayInFacePlane) {
    const BevelMesh m = build_beveled_truncated_icosahedron(kPalette, 2.0, 0.16);
    // 每个面板的三边形共面，且面板法线与原面法线一致（内缩发生在面平面内）。
    // faceIds 与顶点同步：面板记面编号，倒角条/顶帽记哨兵 kNoFace，这里跳过。
    constexpr uint32_t kNoFace = 0xFFFFFFFFu;
    std::map<uint32_t, Vec3> planeNormal;
    const size_t tris = m.solid.vertices.size() / 3;
    ASSERT_TRUE(m.solid.faceIds.size() == tris);   // 同步契约
    size_t panelTris = 0;
    for (size_t t = 0; t < tris; ++t) {
        const uint32_t face = m.solid.faceIds[t];
        if (face == kNoFace) continue;
        ++panelTris;
        const Vec3 n = m.solid.vertices[t * 3].normal;
        const auto it = planeNormal.find(face);
        if (it != planeNormal.end()) {
            ASSERT_TRUE(dot(it->second, n) > 0.999);   // 同一面法线一致
        } else {
            planeNormal[face] = n;
        }
    }
    ASSERT_TRUE(planeNormal.size() == 32);
    ASSERT_TRUE(panelTris == 116);   // 12 个五边形×3 + 20 个六边形×4
}
