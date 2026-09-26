#include "pch.h"
#include "test_framework.h"

int main(int argc, char** argv) {
    // 无缓冲：重定向到文件/管道时是块缓冲，一旦某个用例崩溃，缓冲区会连同
    // "跑到哪一条"的线索一起丢掉（这次排查就吃了这个亏）
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    // --list 只列用例名（按注册顺序）；--filter <子串> 只跑匹配的用例。
    // 用例崩溃时这两个开关是唯一能把范围缩到最小的手段。
    std::string filter;
    bool listOnly = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--list") listOnly = true;
        else if (a == "--filter" && i + 1 < argc) filter = argv[++i];
    }

    int passed = 0;
    int failed = 0;
    for (const auto& t : dtest::Registry()) {
        if (listOnly) {
            std::cout << t.name << "\n";
            continue;
        }
        if (!filter.empty() && std::string(t.name).find(filter) == std::string::npos) continue;
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
