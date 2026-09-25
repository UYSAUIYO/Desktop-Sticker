#include "pch.h"
#include "MediaLibrary.h"

#include "Log.h"
#include "Utf8.h"
#include "desktopsticker/wallpaper/BackendKind.h"
#include "desktopsticker/wallpaper/FfmpegCommand.h"

#include <cwctype>
#include <objbase.h>

namespace fs = std::filesystem;

namespace desktopsticker::wallpaper {

namespace {

// 流式块复制：大文件不整读进内存
bool copy_file_stream(const fs::path& from, const fs::path& to) {
    std::ifstream in(from, std::ios::binary);
    if (!in) return false;
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    if (!out) return false;

    std::vector<char> buffer(1 << 20);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize got = in.gcount();
        if (got > 0) out.write(buffer.data(), got);
    }
    return out.good() || out.eof();
}

bool is_under(const fs::path& child, const fs::path& parent) {
    const auto c = fs::weakly_canonical(child).wstring();
    const auto p = fs::weakly_canonical(parent).wstring();
    return c.size() > p.size() && c.compare(0, p.size(), p) == 0;
}

} // namespace

MediaLibrary::MediaLibrary(std::wstring root, WallPaperStore& store)
    : root_(std::move(root)), store_(store) {}

bool MediaLibrary::IsSafeId(const std::wstring& id) {
    if (id.empty() || id.size() > 64) return false;
    for (wchar_t c : id) {
        const bool ok = (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || c == L'-';
        if (!ok) return false;
    }
    return true;
}

std::wstring MediaLibrary::NewId() {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) return {};
    wchar_t buf[40]{};
    StringFromGUID2(guid, buf, static_cast<int>(std::size(buf)));
    std::wstring s(buf);
    // 去掉花括号并小写，只保留安全字符
    std::wstring out;
    for (wchar_t c : s) {
        if (c == L'{' || c == L'}') continue;
        out.push_back(static_cast<wchar_t>(std::towlower(c)));
    }
    return out;
}

std::wstring MediaLibrary::LibraryJsonPath() const {
    return (fs::path(root_) / L"library.json").wstring();
}

std::wstring MediaLibrary::ItemDir(const std::wstring& id) const {
    return (fs::path(root_) / L"media" / id).wstring();
}

std::wstring MediaLibrary::VariantsDir(const std::wstring& id) const {
    return (fs::path(ItemDir(id)) / L"variants").wstring();
}

std::wstring MediaLibrary::PosterPath(const std::wstring& id) const {
    return (fs::path(ItemDir(id)) / L"poster.png").wstring();
}

std::wstring MediaLibrary::VariantPath(const std::wstring& id, VariantKind kind, int revision) const {
    return (fs::path(VariantsDir(id)) / variant_file_name(kind, revision)).wstring();
}

std::wstring MediaLibrary::SourcePath(const WallPaperItem& item) const {
    return (fs::path(ItemDir(item.id)) / item.sourceFile).wstring();
}

std::vector<WallPaperItem> MediaLibrary::List() const {
    return store_.LoadLibrary(root_);
}

bool MediaLibrary::Update(const std::wstring& id,
                          const std::function<void(WallPaperItem&)>& mutate) {
    auto items = store_.LoadLibrary(root_);
    for (auto& it : items) {
        if (it.id == id) {
            mutate(it);
            return store_.SaveLibrary(root_, items);
        }
    }
    return false;
}

bool MediaLibrary::Rename(const std::wstring& id, const std::wstring& name) {
    if (!IsSafeId(id) || name.empty()) return false;
    return Update(id, [&](WallPaperItem& it) { it.name = name; });
}

bool MediaLibrary::Remove(const std::wstring& id) {
    if (!IsSafeId(id)) return false;

    auto items = store_.LoadLibrary(root_);
    const auto it = std::find_if(items.begin(), items.end(),
                                 [&](const WallPaperItem& x) { return x.id == id; });
    if (it == items.end()) return false;

    // 只删库内该条目的目录，且必须确认它确实位于库根之下
    const fs::path dir(ItemDir(id));
    const fs::path mediaRoot = fs::path(root_) / L"media";
    if (!is_under(dir, mediaRoot)) {
        wp_log("refuse to delete outside library: " + to_utf8(dir.wstring()));
        return false;
    }

    std::error_code ec;
    fs::remove_all(dir, ec);
    if (ec) {
        wp_log("remove item dir failed: " + to_utf8(dir.wstring()) + " : " + ec.message());
        return false;
    }

    items.erase(it);
    return store_.SaveLibrary(root_, items);
}

bool MediaLibrary::Import(const std::wstring& srcPath, std::wstring& outId) {
    outId.clear();

    std::error_code ec;
    if (fs::is_directory(srcPath, ec)) return ImportDirectory(srcPath, outId);

    const fs::path src(srcPath);
    if (!fs::is_regular_file(src, ec)) {
        wp_log("import: source is neither a file nor a directory: " + to_utf8(srcPath));
        return false;
    }

    // 类型由扩展名判定：不设的话动图会被记成视频，后续按视频处理（要音轨、要档位）
    BackendKind kind = BackendKind::Video;
    if (!classify_file(srcPath, kind)) {
        wp_log("import: unsupported file type: " + to_utf8(srcPath));
        return false;
    }

    const std::wstring id = NewId();
    if (id.empty()) return false;

    const fs::path dir(ItemDir(id));
    fs::create_directories(fs::path(dir) / L"variants", ec);
    if (ec) {
        wp_log("import: create item dir failed: " + ec.message());
        return false;
    }

    std::wstring ext = src.extension().wstring();
    if (ext.empty()) ext = L".mp4";
    const std::wstring sourceFileName = L"source" + ext;
    const fs::path dest = dir / sourceFileName;

    // 逐字节复制，绝不改写源文件
    if (!copy_file_stream(src, dest)) {
        wp_log("import: copy failed: " + to_utf8(srcPath));
        fs::remove_all(dir, ec);
        return false;
    }

    WallPaperItem item;
    item.id = id;
    item.name = src.stem().wstring();
    item.sourceFile = sourceFileName;
    item.kind = kind;
    item.sourceBytes = fs::file_size(dest, ec);

    auto items = store_.LoadLibrary(root_);
    items.push_back(std::move(item));
    if (!store_.SaveLibrary(root_, items)) {
        fs::remove_all(dir, ec);
        return false;
    }

    outId = id;
    return true;
}

bool MediaLibrary::ImportDirectory(const std::wstring& srcDir, std::wstring& outId) {
    outId.clear();

    std::error_code ec;
    const fs::path src(srcDir);
    if (!fs::is_directory(src, ec)) {
        wp_log("import: source is not a directory: " + to_utf8(srcDir));
        return false;
    }

    // 目录里有什么决定它是什么类型（index.html → 网页 / .frag → 着色器 / 图片 → 序列）
    std::vector<std::wstring> names;
    for (const auto& e : fs::directory_iterator(src, ec)) {
        names.push_back(e.path().filename().wstring());
    }
    if (ec) {
        wp_log("import: enumerate failed: " + ec.message());
        return false;
    }

    BackendKind kind = BackendKind::ImageSequence;
    if (!classify_directory(names, kind)) {
        wp_log("import: cannot tell the type of directory: " + to_utf8(srcDir));
        return false;
    }

    const std::wstring id = NewId();
    if (id.empty()) return false;

    const fs::path dir(ItemDir(id));
    const fs::path content = dir / backend_kind_content_subdir(kind);
    fs::create_directories(content, ec);
    if (ec) {
        wp_log("import: create item dir failed: " + ec.message());
        return false;
    }

    uint64_t bytes = 0;
    int copied = 0;
    int skipped = 0;

    if (kind == BackendKind::ImageSequence) {
        // 只复制图片帧，非图片一律跳过（规格 §13），且顺序按自然序写库
        const auto images = natural_sort_image_frames(names);
        for (const auto& n : images) {
            const fs::path from = src / n;
            if (!fs::is_regular_file(from, ec)) continue;
            if (!copy_file_stream(from, content / n)) { skipped++; continue; }
            bytes += fs::file_size(content / n, ec);
            copied++;
        }
        skipped += static_cast<int>(names.size() - images.size());
    } else {
        // 网页/着色器是整棵目录树，逐字节复制（含子目录）
        for (const auto& e : fs::recursive_directory_iterator(src, ec)) {
            if (!e.is_regular_file(ec)) continue;
            const fs::path rel = fs::relative(e.path(), src, ec);
            if (ec || rel.empty()) { skipped++; continue; }
            const fs::path to = content / rel;
            fs::create_directories(to.parent_path(), ec);
            if (!copy_file_stream(e.path(), to)) { skipped++; continue; }
            bytes += fs::file_size(to, ec);
            copied++;
        }
    }

    if (copied == 0) {
        wp_log("import: no content copied from " + to_utf8(srcDir));
        fs::remove_all(dir, ec);
        return false;
    }
    if (skipped > 0) {
        wp_log("import: skipped " + std::to_string(skipped) + " file(s) in " +
               to_utf8(srcDir));
    }

    WallPaperItem item;
    item.id = id;
    item.name = src.filename().wstring();
    // 目录型条目的"源"就是内容子目录（request_play 也这么取）
    item.sourceFile = backend_kind_content_subdir(kind);
    item.kind = kind;
    item.sourceBytes = bytes;

    auto items = store_.LoadLibrary(root_);
    items.push_back(std::move(item));
    if (!store_.SaveLibrary(root_, items)) {
        fs::remove_all(dir, ec);
        return false;
    }

    wp_log("import: " + std::to_string(copied) + " file(s) as " +
           to_utf8(backend_kind_name(kind)));
    outId = id;
    return true;
}

} // namespace desktopsticker::wallpaper
