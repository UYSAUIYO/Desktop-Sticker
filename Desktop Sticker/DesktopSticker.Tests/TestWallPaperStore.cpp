#include "pch.h"
#include "test_framework.h"
#include "WallPaperStore.h"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;
using namespace desktopsticker;
using namespace desktopsticker::wallpaper;

namespace {

// 每个用例一个独立临时目录，避免相互污染
struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() /
               ("dstk-wp-test-" + std::to_string(::GetTickCount64()) + "-" +
                std::to_string(rand()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void write_raw(const fs::path& p, const std::string& s) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << s;
}

} // namespace

TEST(WallPaperStore_MissingState_ReturnsDefaults) {
    TempDir tmp;
    WallPaperStore store(tmp.path.wstring());
    const auto state = store.LoadState();
    ASSERT_FALSE(state.settings.enabled);
    ASSERT_TRUE(state.settings.activeId.empty());
    ASSERT_TRUE(state.storage.root.empty());
    ASSERT_FALSE(state.storage.bound);
}

TEST(WallPaperStore_StateRoundTrip) {
    TempDir tmp;
    WallPaperStore store(tmp.path.wstring());

    PersistedState s;
    s.settings.enabled = true;
    s.settings.activeId = L"abc-123";
    s.settings.preferred = VariantKind::PowerSaver;
    s.settings.pauseOnFullscreen = false;
    s.settings.pauseOnLock = true;
    s.storage.root = L"D:\\DesktopSticker\\Wallpaper";
    s.storage.volumeSerial = 4242;
    s.storage.rootFileId = 987654321ull;
    s.storage.bound = true;

    ASSERT_TRUE(store.SaveState(s));

    const auto got = store.LoadState();
    ASSERT_TRUE(got.settings.enabled);
    ASSERT_STREQ(L"abc-123", got.settings.activeId);
    ASSERT_TRUE(got.settings.preferred == VariantKind::PowerSaver);
    ASSERT_FALSE(got.settings.pauseOnFullscreen);
    ASSERT_TRUE(got.settings.pauseOnLock);
    ASSERT_STREQ(L"D:\\DesktopSticker\\Wallpaper", got.storage.root);
    ASSERT_EQ(4242u, got.storage.volumeSerial);
    ASSERT_EQ(987654321ull, got.storage.rootFileId);
    ASSERT_TRUE(got.storage.bound);
    // libraryRoot 是 storage.root 的回显
    ASSERT_STREQ(L"D:\\DesktopSticker\\Wallpaper", got.settings.libraryRoot);
}

TEST(WallPaperStore_CorruptState_DoesNotThrowAndBacksUp) {
    TempDir tmp;
    const fs::path statePath = tmp.path / L"wallpaper.json";
    write_raw(statePath, "{ this is not json ");

    WallPaperStore store(tmp.path.wstring());
    const auto state = store.LoadState();          // 不得抛异常

    ASSERT_FALSE(state.settings.enabled);
    ASSERT_TRUE(state.storage.root.empty());
    ASSERT_TRUE(fs::exists(fs::path(statePath.wstring() + L".bak")));   // 原文件已备份
    ASSERT_FALSE(fs::exists(statePath));                                 // 损坏文件已移走
}

TEST(WallPaperStore_UnknownFields_KeepKnownValues) {
    TempDir tmp;
    const fs::path statePath = tmp.path / L"wallpaper.json";
    write_raw(statePath,
              R"({"version":99,"future":{"x":1},"settings":{"enabled":true,"activeId":"keepme"}})");

    WallPaperStore store(tmp.path.wstring());
    const auto state = store.LoadState();
    ASSERT_TRUE(state.settings.enabled);
    ASSERT_STREQ(L"keepme", state.settings.activeId);
}

TEST(WallPaperStore_LibraryRoundTrip) {
    TempDir tmp;
    WallPaperStore store(tmp.path.wstring());

    std::vector<WallPaperItem> items;
    WallPaperItem a;
    a.id = L"id-a";
    a.name = L"海边日落";
    a.sourceFile = L"source.mp4";
    a.hasPoster = true;
    a.sourceBytes = 12345;
    items.push_back(a);

    WallPaperItem b;
    b.id = L"id-b";
    b.name = L"city";
    b.sourceFile = L"source.mkv";
    b.hasPowerSaver = true;
    items.push_back(b);

    ASSERT_TRUE(store.SaveLibrary(tmp.path.wstring(), items));

    const auto got = store.LoadLibrary(tmp.path.wstring());
    ASSERT_EQ(static_cast<size_t>(2), got.size());
    ASSERT_STREQ(L"id-a", got[0].id);
    ASSERT_STREQ(L"海边日落", got[0].name);      // 中文经 UTF-8 往返不丢
    ASSERT_TRUE(got[0].hasPoster);
    ASSERT_FALSE(got[0].hasBalanced);
    ASSERT_EQ(12345ull, got[0].sourceBytes);
    ASSERT_TRUE(got[1].hasPowerSaver);
}

TEST(WallPaperStore_MissingLibrary_ReturnsEmpty) {
    TempDir tmp;
    WallPaperStore store(tmp.path.wstring());
    ASSERT_TRUE(store.LoadLibrary(tmp.path.wstring()).empty());
}

TEST(WallPaperStore_CorruptLibrary_DoesNotThrow) {
    TempDir tmp;
    write_raw(tmp.path / L"library.json", "not json at all");

    WallPaperStore store(tmp.path.wstring());
    const auto got = store.LoadLibrary(tmp.path.wstring());   // 不得抛异常
    ASSERT_TRUE(got.empty());
    ASSERT_TRUE(fs::exists(fs::path((tmp.path / L"library.json").wstring() + L".bak")));
}

TEST(WallPaperStore_SkipsEntriesWithoutId) {
    TempDir tmp;
    write_raw(tmp.path / L"library.json",
              R"({"version":1,"items":[{"id":"ok","name":"a"},{"name":"noid"}]})");

    WallPaperStore store(tmp.path.wstring());
    const auto got = store.LoadLibrary(tmp.path.wstring());
    ASSERT_EQ(static_cast<size_t>(1), got.size());
    ASSERT_STREQ(L"ok", got[0].id);
}
