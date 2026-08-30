#include "pch.h"
#include "test_framework.h"
#include <ClockWidget.h>

using namespace desktopsticker;

TEST(Clock_TimeZeroPadded) {
    SYSTEMTIME st{2026, 8, 0, 30, 9, 5, 0, 0};
    ASSERT_STREQ(L"09:05", ClockText::TimeText(st));
}

TEST(Clock_TimeAfternoon) {
    SYSTEMTIME st{2026, 8, 0, 30, 23, 59, 0, 0};
    ASSERT_STREQ(L"23:59", ClockText::TimeText(st));
}

TEST(Clock_DateWithWeekday) {
    SYSTEMTIME st{2026, 8, 0, 30, 13, 32, 0, 0}; // wDayOfWeek=0 → 周日
    ASSERT_STREQ(L"2026年8月30日 周日", ClockText::DateText(st));
}

TEST(Clock_WeekdayWraps) {
    SYSTEMTIME st{2026, 1, 6, 3, 0, 0, 0, 0}; // wDayOfWeek=6 → 周六
    ASSERT_STREQ(L"2026年1月3日 周六", ClockText::DateText(st));
}
