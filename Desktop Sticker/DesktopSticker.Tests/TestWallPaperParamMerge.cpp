#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/wallpaper/ParamMerge.h>

#include <limits>

using namespace desktopsticker;
using namespace desktopsticker::wallpaper;
using nlohmann::json;

namespace {
ParamSpec make(std::string key, double def) {
    ParamSpec s;
    s.key = std::move(key);
    s.defaultValue = def;
    return s;
}
ParamSpec ranged(std::string key, double def, double lo, double hi) {
    ParamSpec s = make(std::move(key), def);
    s.hasRange = true;
    s.minValue = lo;
    s.maxValue = hi;
    return s;
}
} // namespace

TEST(ParamMerge_MissingKeyUsesDefault) {
    const json j = json::object();
    ASSERT_TRUE(read_param(j, make("speed", 1.5)) == 1.5);
}

TEST(ParamMerge_WrongTypeUsesDefault) {
    const json j = {{"speed", "fast"}};
    ASSERT_TRUE(read_param(j, make("speed", 1.0)) == 1.0);
}

TEST(ParamMerge_ValueRead) {
    const json j = {{"speed", 2.5}};
    ASSERT_TRUE(read_param(j, make("speed", 1.0)) == 2.5);
}

TEST(ParamMerge_OutOfRangeIsClamped) {
    const json j = {{"amount", 99.0}};
    ASSERT_TRUE(read_param(j, ranged("amount", 1.0, 0.0, 10.0)) == 10.0);
    const json k = {{"amount", -5.0}};
    ASSERT_TRUE(read_param(k, ranged("amount", 1.0, 0.0, 10.0)) == 0.0);
}

TEST(ParamMerge_NoRangeDoesNotClamp) {
    const json j = {{"amount", 1e9}};
    ASSERT_TRUE(read_param(j, make("amount", 1.0)) == 1e9);
}

TEST(ParamMerge_NonFiniteUsesDefault) {
    // 构造一个能装进 json 的非有限值（inf），必须回落默认而不是被当成有效参数
    const json j = { {"amount", std::numeric_limits<double>::infinity()} };
    ASSERT_TRUE(read_param(j, make("amount", 7.0)) == 7.0);
}

TEST(ParamMerge_BatchKeepsSpecOrder) {
    const json j = {{"b", 2.0}};
    const std::vector<ParamSpec> specs = { make("a", 1.0), make("b", 9.0), make("c", 3.0) };
    const auto out = read_params(j, specs);
    ASSERT_EQ(static_cast<size_t>(3), out.size());
    ASSERT_TRUE(out[0] == 1.0);
    ASSERT_TRUE(out[1] == 2.0);
    ASSERT_TRUE(out[2] == 3.0);
}

TEST(ParamMerge_ParseRejectsNonObject) {
    ASSERT_TRUE(parse_params("[1,2,3]").is_object());
    ASSERT_TRUE(parse_params("[1,2,3]").empty());
    ASSERT_TRUE(parse_params("not json").is_object());
    ASSERT_TRUE(parse_params("").is_object());
}

TEST(ParamMerge_ParseAcceptsObject) {
    const auto j = parse_params(R"({"a":1,"b":"x"})");
    ASSERT_TRUE(j.is_object());
    ASSERT_TRUE(j.contains("a"));
}
