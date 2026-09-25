#pragma once

// 3D 渲染要用的最小线性代数（纯函数，列主序，与 GLSL 的 mat4 内存布局一致）。
//
// 列主序的含义：m[col * 4 + row]。上传到 uniform buffer 后 GLSL 里可直接当 mat4 用，
// 不需要转置 —— 这里最容易写反，所以每个函数都标了它在列主序下的落位。
//
// 投影矩阵按 **Vulkan 的裁剪空间**约定（与 OpenGL 不同，这也是移植时的常见坑）：
//   · 深度范围是 [0, 1] 而不是 [-1, 1]
//   · Y 轴向下，所以 y 分量要取负，否则画面上下颠倒

#include <cmath>
#include <cstddef>

namespace desktopsticker::wallpaper {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

inline Vec3 operator-(const Vec3& a, const Vec3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vec3 operator+(const Vec3& a, const Vec3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Vec3 operator*(const Vec3& a, double s) { return { a.x * s, a.y * s, a.z * s }; }
inline double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
inline double length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalize(const Vec3& v) {
    const double len = length(v);
    return len > 0.0 ? v * (1.0 / len) : v;
}

// 列主序 4x4：m[col * 4 + row]
struct Mat4 {
    float m[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
};

inline Mat4 identity() { return Mat4{}; }

// a * b（先应用 b，再应用 a —— 与数学写法一致）
inline Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            r.m[col * 4 + row] = sum;
        }
    }
    return r;
}

// 右手系透视投影，输出 Vulkan 裁剪空间（z ∈ [0,1]，y 向下）
inline Mat4 perspective_vulkan(double fovYRadians, double aspect, double nearZ, double farZ) {
    const double f = 1.0 / std::tan(fovYRadians * 0.5);
    Mat4 r{};
    r.m[0] = static_cast<float>(f / aspect);
    r.m[5] = static_cast<float>(-f);                       // y 取负 = Vulkan 的 y 向下
    r.m[10] = static_cast<float>(farZ / (nearZ - farZ));
    r.m[11] = -1.0f;
    r.m[14] = static_cast<float>(farZ * nearZ / (nearZ - farZ));
    r.m[15] = 0.0f;
    return r;
}

// 右手系 look-at
inline Mat4 look_at(const Vec3& eye, const Vec3& center, const Vec3& up) {
    const Vec3 f = normalize(center - eye);
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);

    Mat4 r{};
    r.m[0] = static_cast<float>(s.x);
    r.m[4] = static_cast<float>(s.y);
    r.m[8] = static_cast<float>(s.z);
    r.m[12] = static_cast<float>(-dot(s, eye));
    r.m[1] = static_cast<float>(u.x);
    r.m[5] = static_cast<float>(u.y);
    r.m[9] = static_cast<float>(u.z);
    r.m[13] = static_cast<float>(-dot(u, eye));
    r.m[2] = static_cast<float>(-f.x);
    r.m[6] = static_cast<float>(-f.y);
    r.m[10] = static_cast<float>(-f.z);
    r.m[14] = static_cast<float>(dot(f, eye));
    r.m[3] = 0.0f;
    r.m[7] = 0.0f;
    r.m[11] = 0.0f;
    r.m[15] = 1.0f;
    return r;
}

// 列主序换算：行主序的 R[row][col] 落在 m[col * 4 + row]。
// 下面两个旋转最容易写反 —— 判据是"右手系下绕轴 +90° 时 +X 应转到 -Z、+Y 转到 +Z"，
// 单测按这个判据锁住（曾经把 col/row 读反一次，整个场景的旋转方向就反了）。
inline Mat4 rotation_x(double radians) {
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    Mat4 r{};
    // R = [[1,0,0],[0,c,-s],[0,s,c]]
    r.m[5] = static_cast<float>(c);    // R[1][1]
    r.m[6] = static_cast<float>(s);    // R[2][1]
    r.m[9] = static_cast<float>(-s);   // R[1][2]
    r.m[10] = static_cast<float>(c);   // R[2][2]
    return r;
}

inline Mat4 rotation_y(double radians) {
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    Mat4 r{};
    // R = [[c,0,s],[0,1,0],[-s,0,c]]
    r.m[0] = static_cast<float>(c);    // R[0][0]
    r.m[2] = static_cast<float>(-s);   // R[2][0]
    r.m[8] = static_cast<float>(s);    // R[0][2]
    r.m[10] = static_cast<float>(c);   // R[2][2]
    return r;
}

// 把模型缩放到给定半径（几何体先按自己的尺度算好，再统一归一化）
inline Mat4 scale_uniform(double s) {
    Mat4 r{};
    r.m[0] = static_cast<float>(s);
    r.m[5] = static_cast<float>(s);
    r.m[10] = static_cast<float>(s);
    return r;
}

// 变换一个点并做透视除法（outW 返回除法前的 w，用来验证近/远平面是否落在 z=0/z=1）
inline Vec3 transform_point(const Mat4& mat, const Vec3& p, double* outW = nullptr) {
    const double x = mat.m[0] * p.x + mat.m[4] * p.y + mat.m[8] * p.z + mat.m[12];
    const double y = mat.m[1] * p.x + mat.m[5] * p.y + mat.m[9] * p.z + mat.m[13];
    const double z = mat.m[2] * p.x + mat.m[6] * p.y + mat.m[10] * p.z + mat.m[14];
    const double w = mat.m[3] * p.x + mat.m[7] * p.y + mat.m[11] * p.z + mat.m[15];
    if (outW) *outW = w;
    if (w != 0.0) return { x / w, y / w, z / w };
    return { x, y, z };
}

} // namespace desktopsticker::wallpaper
