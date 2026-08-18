#include "pch.h"
#include "test_framework.h"

int main() {
    int passed = 0;
    int failed = 0;
    for (const auto& t : dtest::Registry()) {
        try {
            t.fn();
            std::cout << "[PASS] " << t.name << "\n";
            ++passed;
        } catch (const dtest::AssertionFailure& e) {
            std::cout << "[FAIL] " << t.name << ": " << e.message << "\n";
            ++failed;
        } catch (const std::exception& e) {
            std::cout << "[FAIL] " << t.name << ": exception: " << e.what() << "\n";
            ++failed;
        } catch (...) {
            std::cout << "[FAIL] " << t.name << ": unknown exception\n";
            ++failed;
        }
    }
    std::cout << "\n" << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
