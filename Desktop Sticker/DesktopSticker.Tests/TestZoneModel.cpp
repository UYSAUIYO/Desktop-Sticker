#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/ZoneModel.h>

using namespace desktopsticker;

TEST(SaveAndLoad_RoundTrip) {
    auto layoutPath = std::filesystem::temp_directory_path() / L"DesktopStickerTest_layout.json";
    std::filesystem::remove(layoutPath);

    ZoneModel model;
    Zone z;
    z.id = L"z1";
    z.name = L"工作";
    z.rect = RECT{10, 20, 300, 400};
    z.itemPaths.push_back(L"C:\\Users\\test\\Desktop\\微信.lnk");
    model.AddZone(std::move(z));
    model.Layout().originalIconPositions[L"C:\\Users\\test\\Desktop\\微信.lnk"] = POINT{50, 60};

    ASSERT_TRUE(model.Save(layoutPath));

    ZoneModel loaded;
    ASSERT_TRUE(loaded.Load(layoutPath));
    ASSERT_EQ(1u, loaded.Layout().zones.size());
    ASSERT_STREQ(L"工作", loaded.Layout().zones[0].name);
    ASSERT_EQ(10, loaded.Layout().zones[0].rect.left);
    ASSERT_EQ(400, loaded.Layout().zones[0].rect.bottom);
}

TEST(MoveItem_MovesBetweenZones) {
    ZoneModel model;
    Zone a; a.id = L"a"; a.name = L"A";
    Zone b; b.id = L"b"; b.name = L"B";
    a.itemPaths.push_back(L"C:\\x.lnk");
    model.AddZone(std::move(a));
    model.AddZone(std::move(b));

    ASSERT_TRUE(model.MoveItem(L"C:\\x.lnk", L"a", L"b"));
    ASSERT_EQ(0u, model.FindZone(L"a")->itemPaths.size());
    ASSERT_EQ(1u, model.FindZone(L"b")->itemPaths.size());
}
