#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/wallpaper/CoverRect.h>

using namespace desktopsticker::wallpaper;

// cover 缩放：源区域必须居中、宽高比等于目标，且不超出源

TEST(CoverRect_SameAspectIsIdentity) {
    const UvRect r = cover_uv_rect(3840, 2160, 1920, 1080);   // 都是 16:9
    ASSERT_TRUE(std::abs(r.scaleX - 1.0) < 1e-9);
    ASSERT_TRUE(std::abs(r.scaleY - 1.0) < 1e-9);
    ASSERT_TRUE(std::abs(r.offsetX) < 1e-9);
    ASSERT_TRUE(std::abs(r.offsetY) < 1e-9);
}

TEST(CoverRect_WiderSourceCropsLeftAndRight) {
    // 21:9 源铺到 16:9：裁左右，纵向整条保留
    const UvRect r = cover_uv_rect(2560, 1080, 1920, 1080);
    ASSERT_TRUE(std::abs(r.scaleY - 1.0) < 1e-9);
    ASSERT_TRUE(std::abs(r.scaleX - 0.75) < 1e-9);          // 1920/2560
    ASSERT_TRUE(std::abs(r.offsetX - 0.125) < 1e-9);        // 居中
    ASSERT_TRUE(std::abs(r.offsetY) < 1e-9);
    ASSERT_TRUE(r.scaleX * 2560 >= 1920.0 - 1e-6);          // 铺满宽度
}

TEST(CoverRect_TallerSourceCropsTopAndBottom) {
    // 竖屏源铺到 16:9：裁上下
    const UvRect r = cover_uv_rect(1080, 1920, 1920, 1080);
    ASSERT_TRUE(std::abs(r.scaleX - 1.0) < 1e-9);
    ASSERT_TRUE(std::abs(r.scaleY - (1080.0 / 1920.0) / (1920.0 / 1080.0)) < 1e-9);
    ASSERT_TRUE(std::abs(r.offsetY - (1.0 - r.scaleY) * 0.5) < 1e-9);
    ASSERT_TRUE(std::abs(r.offsetX) < 1e-9);
}

TEST(CoverRect_SourceRegionStaysInsideTheTexture) {
    // 任意组合下，采样窗口都不能越界（越界会被 clamp 成拉丝边）
    const int sizes[][2] = { { 1920, 1080 }, { 3840, 2160 }, { 2560, 1080 },
                             { 1080, 1920 }, { 640, 480 }, { 100, 300 } };
    for (const auto& s : sizes) {
        for (const auto& d : sizes) {
            const UvRect r = cover_uv_rect(s[0], s[1], d[0], d[1]);
            ASSERT_TRUE(r.scaleX > 0.0 && r.scaleX <= 1.0 + 1e-9);
            ASSERT_TRUE(r.scaleY > 0.0 && r.scaleY <= 1.0 + 1e-9);
            ASSERT_TRUE(r.offsetX >= -1e-9 && r.offsetX + r.scaleX <= 1.0 + 1e-9);
            ASSERT_TRUE(r.offsetY >= -1e-9 && r.offsetY + r.scaleY <= 1.0 + 1e-9);
        }
    }
}

TEST(CoverRect_InvalidSizeFallsBackToIdentity) {
    const UvRect r = cover_uv_rect(0, 1080, 1920, 1080);
    ASSERT_TRUE(std::abs(r.scaleX - 1.0) < 1e-9 && std::abs(r.scaleY - 1.0) < 1e-9);
    const UvRect r2 = cover_uv_rect(1920, 1080, -5, 1080);
    ASSERT_TRUE(std::abs(r2.scaleX - 1.0) < 1e-9 && std::abs(r2.scaleY - 1.0) < 1e-9);
}

TEST(CoverRect_SquareSourceIntoWideTargetCropsVertically) {
    const UvRect r = cover_uv_rect(1000, 1000, 1920, 1080);
    ASSERT_TRUE(std::abs(r.scaleX - 1.0) < 1e-9);
    ASSERT_TRUE(std::abs(r.scaleY - (1080.0 / 1920.0)) < 1e-9);
    ASSERT_TRUE(r.offsetY > 0.0);
}
