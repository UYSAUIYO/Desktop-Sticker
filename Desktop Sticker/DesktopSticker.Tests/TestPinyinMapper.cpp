#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/PinyinMapper.h>

using namespace desktopsticker;

TEST(WeChat_ReturnsWx) {
    ASSERT_STREQ(L"wx", PinyinMapper::GetInitials(L"微信"));
}

TEST(Browser_ReturnsLlq) {
    ASSERT_STREQ(L"llq", PinyinMapper::GetInitials(L"浏览器"));
}

TEST(MixedText_KeepsAscii) {
    ASSERT_STREQ(L"wxtest", PinyinMapper::GetInitials(L"微信test"));
}
