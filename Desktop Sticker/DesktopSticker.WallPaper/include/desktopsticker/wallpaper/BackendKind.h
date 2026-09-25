#pragma once

// 后端类型识别：从"用户给的东西"推断壁纸后端类型。
// 纯函数，只看路径与目录清单，不碰文件系统。

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace desktopsticker::wallpaper {

enum class BackendKind { Video, AnimatedImage, ImageSequence, Web, Shader3D };

namespace detail {

inline std::wstring lower_copy(std::wstring s) {
    for (wchar_t& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

inline std::wstring file_name(const std::wstring& p) {
    const size_t pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? p : p.substr(pos + 1);
}

inline std::wstring extension(const std::wstring& p) {
    const std::wstring n = file_name(p);
    const size_t pos = n.find_last_of(L'.');
    return pos == std::wstring::npos ? std::wstring{} : lower_copy(n.substr(pos));
}

} // namespace detail

// 单文件的扩展名 → 类型；不认识返回 nullopt 语义（用 Web 兜底不合适，故返回 bool）
inline bool classify_file(const std::wstring& path, BackendKind& out) {
    const std::wstring ext = detail::extension(path);
    if (ext == L".mp4" || ext == L".mov" || ext == L".avi" || ext == L".mkv" ||
        ext == L".wmv" || ext == L".webm" || ext == L".m4v" || ext == L".mpg" ||
        ext == L".mpeg" || ext == L".ts") {
        out = BackendKind::Video;
        return true;
    }
    if (ext == L".gif" || ext == L".webp" || ext == L".apng") {
        out = BackendKind::AnimatedImage;
        return true;
    }
    if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".bmp" ||
        ext == L".tif" || ext == L".tiff") {
        // 单张静态图片按"单帧序列"处理，走同一后端
        out = BackendKind::ImageSequence;
        return true;
    }
    if (ext == L".html" || ext == L".htm") {
        out = BackendKind::Web;
        return true;
    }
    if (ext == L".frag" || ext == L".glsl" || ext == L".vert") {
        out = BackendKind::Shader3D;
        return true;
    }
    if (ext == L".gltf" || ext == L".glb" || ext == L".obj") {
        out = BackendKind::Shader3D;
        return true;
    }
    return false;
}

// 目录 → 类型。优先级（按"最具体优先"）：
//   web（含 index.html） > shader（含 .frag/.glsl/.gltf/.glb/.obj） > 序列（含图片）
// 目录里全是图片 → 图片序列；空目录或都不匹配 → 无法判定
inline bool classify_directory(const std::vector<std::wstring>& entries, BackendKind& out) {
    bool hasIndexHtml = false;
    bool hasShader = false;
    bool hasImage = false;

    for (const auto& e : entries) {
        const std::wstring ext = detail::extension(e);
        const std::wstring name = detail::lower_copy(detail::file_name(e));
        if (name == L"index.html" || name == L"index.htm") hasIndexHtml = true;
        if (ext == L".frag" || ext == L".glsl" || ext == L".vert" || ext == L".comp" ||
            ext == L".gltf" || ext == L".glb" || ext == L".obj") {
            hasShader = true;
        }
        if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".bmp" ||
            ext == L".tif" || ext == L".tiff" || ext == L".webp") {
            hasImage = true;
        }
    }

    if (hasIndexHtml) { out = BackendKind::Web; return true; }
    if (hasShader) { out = BackendKind::Shader3D; return true; }
    if (hasImage) { out = BackendKind::ImageSequence; return true; }
    return false;
}

// 图片序列的帧文件名筛选 + 自然排序（img2 在 img10 之前）。
// 只保留图片扩展名；非图片一律丢弃。
inline std::vector<std::wstring> natural_sort_image_frames(const std::vector<std::wstring>& names) {
    std::vector<std::wstring> images;
    for (const auto& n : names) {
        const std::wstring ext = detail::extension(n);
        if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".bmp" ||
            ext == L".tif" || ext == L".tiff" || ext == L".webp") {
            images.push_back(n);
        }
    }

    auto less = [](const std::wstring& a, const std::wstring& b) {
        size_t i = 0, j = 0;
        while (i < a.size() && j < b.size()) {
            const bool da = iswdigit(a[i]) != 0;
            const bool db = iswdigit(b[j]) != 0;
            if (da && db) {
                // 取整段数字比较数值，且**忽略前导零**（img02 == img2）
                size_t ia = i, jb = j;
                while (ia < a.size() && iswdigit(a[ia])) ++ia;
                while (jb < b.size() && iswdigit(b[jb])) ++jb;
                std::wstring na = a.substr(i, ia - i), nb = b.substr(j, jb - j);
                const size_t za = na.find_first_not_of(L'0');
                const size_t zb = nb.find_first_not_of(L'0');
                na = (za == std::wstring::npos) ? L"0" : na.substr(za);
                nb = (zb == std::wstring::npos) ? L"0" : nb.substr(zb);
                if (na.size() != nb.size()) return na.size() < nb.size();
                if (na != nb) return na < nb;
                i = ia;
                j = jb;
                continue;
            }
            const wchar_t ca = static_cast<wchar_t>(towlower(a[i]));
            const wchar_t cb = static_cast<wchar_t>(towlower(b[j]));
            if (ca != cb) return ca < cb;
            ++i;
            ++j;
        }
        // 前缀相同则短的在前，保证严格弱序
        return a.size() < b.size();
    };

    std::sort(images.begin(), images.end(), less);
    return images;
}

} // namespace desktopsticker::wallpaper
