#include "pch.h"
#include "test_framework.h"
#include "MediaLibrary.h"

#include <filesystem>

namespace fs = std::filesystem;
using namespace desktopsticker;
using namespace desktopsticker::wallpaper;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() /
               ("dstk-wp-lib-" + std::to_string(::GetTickCount64()) + "-" +
                std::to_string(rand()));
        fs::create_directories(path / L"lib");
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

// 造一个内容确定的源文件，用于字节比对
fs::path make_source(const fs::path& dir) {
    const fs::path src = dir / L"我的视频.mp4";
    std::ofstream out(src, std::ios::binary | std::ios::trunc);
    for (int i = 0; i < 4096; ++i) out.put(static_cast<char>(i % 251));
    out.close();
    return src;
}

std::string read_all(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

} // namespace

TEST(MediaLibrary_ImportCopiesBytesAndKeepsSourceIntact) {
    TempDir tmp;
    const fs::path src = make_source(tmp.path);
    const std::string before = read_all(src);

    WallPaperStore store(tmp.path.wstring());
    MediaLibrary lib((tmp.path / L"lib").wstring(), store);

    std::wstring id;
    ASSERT_TRUE(lib.Import(src.wstring(), id));
    ASSERT_FALSE(id.empty());

    // 源文件必须原封不动且仍然存在
    ASSERT_TRUE(fs::exists(src));
    ASSERT_TRUE(read_all(src) == before);

    // 库内副本与源文件字节一致
    const auto items = lib.List();
    ASSERT_EQ(static_cast<size_t>(1), items.size());
    ASSERT_STREQ(L"我的视频", items[0].name);
    ASSERT_EQ(static_cast<uint64_t>(before.size()), items[0].sourceBytes);
    ASSERT_TRUE(read_all(fs::path(lib.SourcePath(items[0]))) == before);
}

TEST(MediaLibrary_ImportRejectsNonFile) {
    TempDir tmp;
    WallPaperStore store(tmp.path.wstring());
    MediaLibrary lib((tmp.path / L"lib").wstring(), store);

    std::wstring id;
    ASSERT_FALSE(lib.Import((tmp.path / L"nope.mp4").wstring(), id));
    ASSERT_TRUE(id.empty());
    ASSERT_TRUE(lib.List().empty());
}

TEST(MediaLibrary_Rename) {
    TempDir tmp;
    const fs::path src = make_source(tmp.path);
    WallPaperStore store(tmp.path.wstring());
    MediaLibrary lib((tmp.path / L"lib").wstring(), store);

    std::wstring id;
    ASSERT_TRUE(lib.Import(src.wstring(), id));
    ASSERT_TRUE(lib.Rename(id, L"新名字"));

    const auto items = lib.List();
    ASSERT_EQ(static_cast<size_t>(1), items.size());
    ASSERT_STREQ(L"新名字", items[0].name);
}

TEST(MediaLibrary_RemoveDeletesCopyButKeepsUserSource) {
    TempDir tmp;
    const fs::path src = make_source(tmp.path);
    const std::string before = read_all(src);

    WallPaperStore store(tmp.path.wstring());
    MediaLibrary lib((tmp.path / L"lib").wstring(), store);

    std::wstring id;
    ASSERT_TRUE(lib.Import(src.wstring(), id));
    ASSERT_TRUE(fs::exists(fs::path(lib.ItemDir(id))));

    ASSERT_TRUE(lib.Remove(id));

    ASSERT_FALSE(fs::exists(fs::path(lib.ItemDir(id))));   // 库内副本已删
    ASSERT_TRUE(fs::exists(src));                          // 用户原始文件仍在
    ASSERT_TRUE(read_all(src) == before);
    ASSERT_TRUE(lib.List().empty());
}

TEST(MediaLibrary_RejectsUnsafeId) {
    // 路径逃逸防护：id 只允许字母数字与短横
    ASSERT_TRUE(MediaLibrary::IsSafeId(L"a1b2-c3"));
    ASSERT_FALSE(MediaLibrary::IsSafeId(L"..\\..\\windows"));
    ASSERT_FALSE(MediaLibrary::IsSafeId(L"a/b"));
    ASSERT_FALSE(MediaLibrary::IsSafeId(L""));
    ASSERT_FALSE(MediaLibrary::IsSafeId(L"a b"));
}

TEST(MediaLibrary_NewIdIsSafeAndUnique) {
    const std::wstring a = MediaLibrary::NewId();
    const std::wstring b = MediaLibrary::NewId();
    ASSERT_TRUE(MediaLibrary::IsSafeId(a));
    ASSERT_TRUE(MediaLibrary::IsSafeId(b));
    ASSERT_TRUE(a != b);
}

TEST(MediaLibrary_RemoveUnknownIdReturnsFalse) {
    TempDir tmp;
    WallPaperStore store(tmp.path.wstring());
    MediaLibrary lib((tmp.path / L"lib").wstring(), store);
    ASSERT_FALSE(lib.Remove(L"00000000-0000-0000-0000-000000000000"));
}
