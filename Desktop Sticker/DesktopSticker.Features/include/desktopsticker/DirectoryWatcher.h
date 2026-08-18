#pragma once
#include <atomic>
#include <filesystem>
#include <functional>
#include <thread>

#include "desktopsticker/Export.h"

namespace desktopsticker {

class DESKTOPSTICKER_API DirectoryWatcher {
public:
    using Callback = std::function<void()>;

    DirectoryWatcher(std::filesystem::path dir, Callback callback);
    ~DirectoryWatcher();

    bool Start();
    void Stop();

private:
    void ThreadMain();

    std::filesystem::path dir_;
    Callback callback_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

} // namespace desktopsticker
