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

TEST(WallPaperPackage_ParsesOptionalAuthorMetadata) {
    const std::string text = R"json({
      "format_version": 1,
      "title": "地球",
      "main_scene": "res://main.tscn",
      "author": "sykm",
      "description": "实时地球 3D",
      "pack_id": "dswall-1234",
      "tags": ["自然", "HDR"],
      "categories": ["3D"],
      "requested_permissions": ["network", "exec"]
    })json";
    WallPaperManifest m;
    std::string error;
    ASSERT_TRUE(parse_wallpaper_manifest(text, m, error));
    ASSERT_TRUE(m.author == "sykm");
    ASSERT_TRUE(m.description == "实时地球 3D");
    ASSERT_TRUE(m.packId == "dswall-1234");
    ASSERT_EQ(static_cast<size_t>(2), m.tags.size());
    ASSERT_TRUE(m.tags[0] == "自然");
    ASSERT_EQ(static_cast<size_t>(1), m.categories.size());
    ASSERT_EQ(static_cast<size_t>(2), m.requestedPermissions.size());
    ASSERT_TRUE(m.requestedPermissions[0] == "network");
    ASSERT_TRUE(m.requestedPermissions[1] == "exec");
}

TEST(WallPaperPackage_AuthorMetadataOptional) {
    WallPaperManifest m;
    std::string error;
    // 旧包没有任何新字段：仍解析成功，新字段取空。
    ASSERT_TRUE(parse_wallpaper_manifest(
        R"({"format_version": 1, "main_scene": "res://m.tscn"})", m, error));
    ASSERT_TRUE(m.author.empty());
    ASSERT_TRUE(m.packId.empty());
    ASSERT_TRUE(m.tags.empty());
    ASSERT_TRUE(m.requestedPermissions.empty());
    // 非字符串元素被跳过而非报错。
    WallPaperManifest m2;
    ASSERT_TRUE(parse_wallpaper_manifest(
        R"({"format_version":1,"main_scene":"r","tags":["a",5,null,"b"]})", m2, error));
    ASSERT_EQ(static_cast<size_t>(2), m2.tags.size());
}
