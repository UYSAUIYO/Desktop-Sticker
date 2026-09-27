#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/wallpaper/WallPaperPackage.h>

using namespace desktopsticker::wallpaper;

TEST(WallPaperPackage_ParsesManifest) {
    const std::string text = R"json({
      "format_version": 1,
      "title": "测试场景",
      "main_scene": "res://main.tscn",
      "engine": { "version": "4.7.2", "build": "4.7.2.stable (custom_build)" },
      "requested_permissions": []
    })json";
    WallPaperManifest m;
    std::string error;
    ASSERT_TRUE(parse_wallpaper_manifest(text, m, error));
    ASSERT_EQ(1, m.formatVersion);
    ASSERT_TRUE(m.title == "测试场景");
    ASSERT_TRUE(m.mainScene == "res://main.tscn");
    ASSERT_TRUE(m.engineVersion == "4.7.2");
    ASSERT_TRUE(m.engineBuild.rfind("4.7.2.stable", 0) == 0);
}

TEST(WallPaperPackage_RejectsGarbage) {
    WallPaperManifest m;
    std::string error;
    ASSERT_FALSE(parse_wallpaper_manifest("not json at all {", m, error));
    ASSERT_FALSE(error.empty());
    ASSERT_FALSE(parse_wallpaper_manifest("[1,2,3]", m, error));
}

TEST(WallPaperPackage_RejectsWrongFormatVersion) {
    WallPaperManifest m;
    std::string error;
    ASSERT_FALSE(parse_wallpaper_manifest(
        R"({"format_version": 2, "main_scene": "res://main.tscn"})", m, error));
    ASSERT_FALSE(parse_wallpaper_manifest(R"({"main_scene": "res://main.tscn"})", m, error));
}

TEST(WallPaperPackage_RejectsMissingMainScene) {
    WallPaperManifest m;
    std::string error;
    ASSERT_FALSE(parse_wallpaper_manifest(R"({"format_version": 1})", m, error));
    ASSERT_TRUE(m.mainScene.empty());
}

TEST(WallPaperPackage_EngineBlockOptional) {
    WallPaperManifest m;
    std::string error;
    ASSERT_TRUE(parse_wallpaper_manifest(
        R"({"format_version": 1, "main_scene": "res://level.tscn"})", m, error));
    ASSERT_TRUE(m.engineVersion.empty());
    ASSERT_TRUE(m.title.empty());
}

TEST(WallPaperPackage_PackEntryDetection) {
    ASSERT_TRUE(has_wallpaper_pack_entry({ L"Project.PCK", L"manifest.json" }));
    ASSERT_TRUE(has_wallpaper_pack_entry({ L"project.pck" }));
    ASSERT_FALSE(has_wallpaper_pack_entry({ L"manifest.json", L"poster.png" }));
    ASSERT_FALSE(has_wallpaper_pack_entry({}));
}
