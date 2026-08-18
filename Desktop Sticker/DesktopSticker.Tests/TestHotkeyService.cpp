#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/HotkeyService.h>

#include <memory>

using namespace desktopsticker;

TEST(DoubleSpaceWithinWindow_Triggers) {
    auto now = std::make_shared<long long>(0);
    HotkeyService svc([now]() { return *now; });
    svc.SetTextInputPredicate([]() { return false; });

    ASSERT_FALSE(svc.HandleKeyEvent(true, *now += 100));   // 第一次按下
    ASSERT_FALSE(svc.HandleKeyEvent(false, *now += 50));   // 释放
    ASSERT_TRUE(svc.HandleKeyEvent(true, *now += 100));    // 第二次按下（间隔 100ms ≤ 250ms）
}

TEST(DoubleSpaceTooSlow_DoesNotTrigger) {
    auto now = std::make_shared<long long>(0);
    HotkeyService svc([now]() { return *now; });
    svc.SetTextInputPredicate([]() { return false; });

    ASSERT_FALSE(svc.HandleKeyEvent(true, *now += 100));
    ASSERT_FALSE(svc.HandleKeyEvent(false, *now += 50));
    ASSERT_FALSE(svc.HandleKeyEvent(true, *now += 500));   // 间隔 500ms > 250ms
}

TEST(TextInputForeground_DoesNotTrigger) {
    auto now = std::make_shared<long long>(0);
    HotkeyService svc([now]() { return *now; });
    svc.SetTextInputPredicate([]() { return true; });

    ASSERT_FALSE(svc.HandleKeyEvent(true, *now += 100));
    ASSERT_FALSE(svc.HandleKeyEvent(false, *now += 50));
    ASSERT_FALSE(svc.HandleKeyEvent(true, *now += 100));   // 即使节奏正确也不触发
}

TEST(DoublePress_CallbackInvoked) {
    auto now = std::make_shared<long long>(0);
    HotkeyService svc([now]() { return *now; });
    svc.SetTextInputPredicate([]() { return false; });
    int calls = 0;
    svc.SetOnDoublePress([&calls]() { ++calls; });

    svc.HandleKeyEvent(true, *now += 100);
    svc.HandleKeyEvent(false, *now += 50);
    svc.HandleKeyEvent(true, *now += 100);
    ASSERT_EQ(1, calls);
}
