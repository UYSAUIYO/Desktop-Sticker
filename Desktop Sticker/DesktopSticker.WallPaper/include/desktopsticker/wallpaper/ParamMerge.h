#pragma once

// 壁纸参数（着色器 uniforms / 粒子参数 / 播放参数）的 JSON 合并。
// 纯函数：缺字段取默认、类型不符忽略、数值越界钳制。

#include <cmath>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace desktopsticker::wallpaper {

struct ParamSpec {
    std::string key;        // JSON 键
    double defaultValue = 0.0;
    double minValue = 0.0;
    double maxValue = 0.0;
    bool hasRange = false;
};

// 按 spec 从 json 取数值参数；缺失/类型不符/非有限值 → 默认值；有区间则钳制。
inline double read_param(const nlohmann::json& j, const ParamSpec& spec) {
    auto it = j.find(spec.key);
    if (it == j.end() || !it->is_number()) return spec.defaultValue;

    const double v = it->get<double>();
    if (!std::isfinite(v)) return spec.defaultValue;
    if (spec.hasRange) {
        if (v < spec.minValue) return spec.minValue;
        if (v > spec.maxValue) return spec.maxValue;
    }
    return v;
}

// 批量读取，返回与 specs 同序的值
inline std::vector<double> read_params(const nlohmann::json& j,
                                       const std::vector<ParamSpec>& specs) {
    std::vector<double> out;
    out.reserve(specs.size());
    for (const auto& s : specs) out.push_back(read_param(j, s));
    return out;
}

// 解析参数文件内容；不是对象或解析失败 → 空对象（调用方据此全用默认值）
inline nlohmann::json parse_params(const std::string& utf8) {
    try {
        auto j = nlohmann::json::parse(utf8);
        if (!j.is_object()) return nlohmann::json::object();
        return j;
    } catch (...) {
        return nlohmann::json::object();
    }
}

} // namespace desktopsticker::wallpaper
