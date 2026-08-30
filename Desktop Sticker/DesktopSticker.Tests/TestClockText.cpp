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

TEST(Clock_TimeSec) {
    SYSTEMTIME st{2026, 8, 0, 30, 14, 2, 33, 0};
    ASSERT_STREQ(L"14:02:33", ClockText::TimeSecText(st));
    SYSTEMTIME midnight{2026, 8, 0, 30, 0, 0, 0, 0};
    ASSERT_STREQ(L"00:00:00", ClockText::TimeSecText(midnight));
}

TEST(Clock_WeatherDesc) {
    ASSERT_STREQ(L"晴", ClockText::WeatherDesc(0));
    ASSERT_STREQ(L"多云", ClockText::WeatherDesc(2));
    ASSERT_STREQ(L"雾", ClockText::WeatherDesc(48));
    ASSERT_STREQ(L"雨", ClockText::WeatherDesc(63));
    ASSERT_STREQ(L"阵雨", ClockText::WeatherDesc(81));
    ASSERT_STREQ(L"雷雨", ClockText::WeatherDesc(95));
}

TEST(Clock_WindText) {
    ASSERT_STREQ(L"西北风 3级", ClockText::WindText(315, 4.0));
    ASSERT_STREQ(L"东北风 1级", ClockText::WindText(45, 0.5));
    ASSERT_STREQ(L"无风", ClockText::WindText(0, 0.1));
    ASSERT_STREQ(L"北风 12级", ClockText::WindText(350, 33.0));
}

TEST(Clock_MinutesOfDay) {
    ASSERT_EQ(392, ClockText::MinutesOfDay(L"06:32"));
    ASSERT_EQ(1155, ClockText::MinutesOfDay(L"19:15"));
    ASSERT_EQ(0, ClockText::MinutesOfDay(L"00:00"));
    ASSERT_EQ(-1, ClockText::MinutesOfDay(L"xx"));
    ASSERT_EQ(-1, ClockText::MinutesOfDay(L"25:00"));
}
