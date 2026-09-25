#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/resmon/JsonBuild.h>

#include <nlohmann/json.hpp>

using namespace desktopsticker::resmon;
using nlohmann::json;

TEST(JsonBuild_Utf8RoundTripChinese) {
    ASSERT_STREQ(L"壁纸媒体库", from_utf8(to_utf8(L"壁纸媒体库")).c_str());
}

TEST(JsonBuild_Utf8RoundTripPathWithBackslashes) {
    const std::wstring p = L"E:\\DesktopSticker\\Wallpaper\\media\\测试\\source.mp4";
    ASSERT_STREQ(p.c_str(), from_utf8(to_utf8(p)).c_str());
}

TEST(JsonBuild_StorageResponseRoundTripsThroughJsonParser) {
    StorageSnapshot s;
    s.totalBytes = 1024;
    s.scannedAtMs = 1234567890;
    s.rows.push_back({StorageCategory::Wallpaper, 1000, 97.7});
    s.rows.push_back({StorageCategory::Pdb, 24, 2.3});
    s.warnings.push_back(L"路径不存在: E:\\不存在的目录");

    const std::string dumped = build_storage_response(s);
    const json j = json::parse(dumped); // 必须能被解析

    ASSERT_EQ(std::string("storage"), j.at("type").get<std::string>());
    ASSERT_EQ(1024ull, j.at("totalBytes").get<uint64_t>());
    ASSERT_EQ(2u, static_cast<unsigned>(j.at("categories").size()));
    // 反斜杠与中文经 JSON 往返不丢
    ASSERT_STREQ(L"路径不存在: E:\\不存在的目录",
                 from_utf8(j.at("warnings").at(0).get<std::string>()).c_str());
    ASSERT_STREQ(L"壁纸媒体库",
                 from_utf8(j.at("categories").at(0).at("name").get<std::string>()).c_str());
}

TEST(JsonBuild_StorageNoteOnlyForActionableCategories) {
    StorageSnapshot s;
    s.rows.push_back({StorageCategory::Pdb, 1, 1.0});
    s.rows.push_back({StorageCategory::Runtime, 1, 1.0});
    s.rows.push_back({StorageCategory::Logs, 1, 1.0});

    const json j = json::parse(build_storage_response(s));
    ASSERT_TRUE(j.at("categories").at(0).contains("note"));      // Pdb 有备注
    ASSERT_TRUE(j.at("categories").at(1).contains("note"));      // Runtime 有备注
    ASSERT_FALSE(j.at("categories").at(2).contains("note"));     // Logs 无备注
    ASSERT_TRUE(j.at("categories").at(0).at("color").get<std::string>()[0] == '#');
}

TEST(JsonBuild_CpuResponseShape) {
    CpuSnapshot s;
    s.baseline = false;
    s.totalPercent = 12.5;
    s.threads.push_back({1234, L"壁纸渲染与帧调度", 8.0, 800, false});
    s.threads.push_back({2000, L"", 1.0, 100, false});           // 未命名线程
    s.children.push_back({4321, L"ffmpeg.exe", 40.0, 1024 * 1024});

    const json j = json::parse(build_cpu_response(s, 999));
    ASSERT_EQ(std::string("cpu"), j.at("type").get<std::string>());
    ASSERT_EQ(999, j.at("sampledAtMs").get<int64_t>());
    ASSERT_EQ(2u, static_cast<unsigned>(j.at("threads").size()));
    ASSERT_STREQ(L"壁纸渲染与帧调度",
                 from_utf8(j.at("threads").at(0).at("name").get<std::string>()).c_str());
    // 未命名线程必须给出可读占位，而不是空串
    ASSERT_STREQ(L"线程 2000",
                 from_utf8(j.at("threads").at(1).at("name").get<std::string>()).c_str());
    ASSERT_EQ(1u, static_cast<unsigned>(j.at("children").size()));
}

TEST(JsonBuild_MemoryResponseShape) {
    MemorySnapshot s;
    s.workingSetBytes = 200ull * 1024 * 1024;
    s.privateBytes = 150ull * 1024 * 1024;
    s.peakWorkingSetBytes = 300ull * 1024 * 1024;
    s.modules.push_back({L"DesktopSticker.ResMon.dll", 4096});

    const json j = json::parse(build_memory_response(s));
    ASSERT_EQ(std::string("memory"), j.at("type").get<std::string>());
    ASSERT_EQ(200ull * 1024 * 1024, j.at("workingSetBytes").get<uint64_t>());
    ASSERT_EQ(1u, static_cast<unsigned>(j.at("modules").size()));
    ASSERT_STREQ(L"DesktopSticker.ResMon.dll",
                 from_utf8(j.at("modules").at(0).at("name").get<std::string>()).c_str());
}

TEST(JsonBuild_ErrorResponse) {
    const json j = json::parse(build_error_response(L"storage", L"扫描失败"));
    ASSERT_EQ(std::string("error"), j.at("type").get<std::string>());
    ASSERT_STREQ(L"扫描失败", from_utf8(j.at("message").get<std::string>()).c_str());
}

TEST(JsonBuild_EmptySnapshotsAreValidJson) {
    ASSERT_TRUE(json::parse(build_cpu_response(CpuSnapshot{}, 0)).at("threads").empty());
    ASSERT_TRUE(json::parse(build_memory_response(MemorySnapshot{})).at("modules").empty());
    ASSERT_TRUE(json::parse(build_storage_response(StorageSnapshot{})).at("categories").empty());
}
