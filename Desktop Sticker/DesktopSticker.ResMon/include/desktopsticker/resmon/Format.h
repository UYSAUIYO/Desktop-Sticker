#pragma once

#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

namespace desktopsticker::resmon {

namespace detail {
inline std::wstring strip_trailing_zero(std::wstring s) {
    if (s.find(L'.') == std::wstring::npos) return s;
    while (!s.empty() && s.back() == L'0') s.pop_back();
    if (!s.empty() && s.back() == L'.') s.pop_back();
    return s;
}
} // namespace detail

// 1024 进制；B 为整数，KB 及以上保留 1 位小数并去掉末尾的 .0
// （与参考形态的 "4 KB" / "54.3 GB" 一致）
inline std::wstring format_bytes(uint64_t bytes) {
    static const wchar_t* kUnits[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
    if (bytes < 1024ull) return std::to_wstring(bytes) + L" B";

    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    wchar_t buf[64]{};
    swprintf_s(buf, L"%.1f", value);
    return detail::strip_trailing_zero(std::wstring(buf)) + L" " + kUnits[unit];
}

inline std::wstring format_percent(double percent) {
    wchar_t buf[64]{};
    swprintf_s(buf, L"%.1f", percent);
    return detail::strip_trailing_zero(std::wstring(buf)) + L"%";
}

// 同序百分比：保留 1 位小数；四舍五入的余量由最大类补差吸收，保证合计恰为 100.0
// （总计为 0 时全部返回 0.0）。并列最大时取下标最小者，结果确定。
inline std::vector<double> compute_percentages(const std::vector<uint64_t>& bytes) {
    std::vector<double> out(bytes.size(), 0.0);
    if (bytes.empty()) return out;

    uint64_t total = 0;
    for (uint64_t b : bytes) total += b;
    if (total == 0) return out;

    std::vector<long long> tenths(bytes.size(), 0);
    long long sum = 0;
    size_t largest = 0;
    for (size_t i = 0; i < bytes.size(); ++i) {
        const double raw = static_cast<double>(bytes[i]) * 1000.0 / static_cast<double>(total);
        tenths[i] = static_cast<long long>(raw + 0.5); // 以 0.1% 为单位
        sum += tenths[i];
        if (bytes[i] > bytes[largest]) largest = i;
    }
    tenths[largest] += (1000 - sum); // 最大类补差
    if (tenths[largest] < 0) tenths[largest] = 0;

    for (size_t i = 0; i < bytes.size(); ++i) out[i] = static_cast<double>(tenths[i]) / 10.0;
    return out;
}

} // namespace desktopsticker::resmon
