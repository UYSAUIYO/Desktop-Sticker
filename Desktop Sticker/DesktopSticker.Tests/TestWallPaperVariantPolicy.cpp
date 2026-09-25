#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/wallpaper/VariantPolicy.h>

using namespace desktopsticker;
using namespace desktopsticker::wallpaper;

TEST(VariantPolicy_OriginalAlwaysOriginal) {
    VariantAvailability a{true, true};
    ASSERT_TRUE(resolve_effective_variant(VariantKind::Original, a) == VariantKind::Original);
}

TEST(VariantPolicy_BalancedWhenPresent) {
    VariantAvailability a{true, false};
    ASSERT_TRUE(resolve_effective_variant(VariantKind::Balanced, a) == VariantKind::Balanced);
}

TEST(VariantPolicy_BalancedMissing_FallsBackToOriginal) {
    VariantAvailability a{false, true};
    ASSERT_TRUE(resolve_effective_variant(VariantKind::Balanced, a) == VariantKind::Original);
}

TEST(VariantPolicy_PowerSaverWhenPresent) {
    VariantAvailability a{false, true};
    ASSERT_TRUE(resolve_effective_variant(VariantKind::PowerSaver, a) == VariantKind::PowerSaver);
}

TEST(VariantPolicy_PowerSaverMissing_FallsBackToOriginal) {
    VariantAvailability a{true, false};
    ASSERT_TRUE(resolve_effective_variant(VariantKind::PowerSaver, a) == VariantKind::Original);
}
