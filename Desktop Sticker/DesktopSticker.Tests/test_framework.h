#pragma once
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace dtest {

struct TestCase {
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& Registry() {
    static std::vector<TestCase> reg;
    return reg;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { Registry().push_back({name, fn}); }
};

struct AssertionFailure {
    std::string message;
};

inline void Fail(const char* expr, const char* file, int line) {
    std::ostringstream os;
    os << "Assertion failed: " << expr << " at " << file << ":" << line;
    throw AssertionFailure{os.str()};
}

inline void FailMsg(const std::string& msg, const char* file, int line) {
    std::ostringstream os;
    os << msg << " at " << file << ":" << line;
    throw AssertionFailure{os.str()};
}

} // namespace dtest

#define TEST(name) \
    static void name(); \
    static ::dtest::Registrar registrar_##name(#name, name); \
    static void name()

#define ASSERT_TRUE(cond) \
    do { if (!(cond)) ::dtest::Fail(#cond, __FILE__, __LINE__); } while (0)

#define ASSERT_FALSE(cond) \
    do { if (cond) ::dtest::Fail(#cond, __FILE__, __LINE__); } while (0)

#define ASSERT_EQ(a, b) \
    do { if (!((a) == (b))) ::dtest::FailMsg(std::string("ASSERT_EQ failed: ") + #a + " == " + #b, __FILE__, __LINE__); } while (0)

#define ASSERT_STREQ(a, b) \
    do { if (std::wstring(a) != std::wstring(b)) ::dtest::FailMsg(std::string("ASSERT_STREQ failed: ") + #a + " == " + #b, __FILE__, __LINE__); } while (0)
