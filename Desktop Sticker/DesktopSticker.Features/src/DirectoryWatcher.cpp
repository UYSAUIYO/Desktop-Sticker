#include "pch.h"
#include "desktopsticker/DirectoryWatcher.h"

namespace desktopsticker {

DirectoryWatcher::DirectoryWatcher(std::filesystem::path dir, Callback callback)
    : dir_(std::move(dir)), callback_(std::move(callback)) {}

DirectoryWatcher::~DirectoryWatcher() {
    Stop();
}

bool DirectoryWatcher::Start() {
    if (running_.exchange(true)) return false;
    handle_ = CreateFileW(dir_.c_str(), FILE_LIST_DIRECTORY,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
        running_.store(false);
        return false;
    }
    thread_ = std::thread([this]() { ThreadMain(); });
    return true;
}

void DirectoryWatcher::Stop() {
    if (!running_.exchange(false)) return;
    if (handle_ != INVALID_HANDLE_VALUE) {
        CancelIoEx(handle_, nullptr);
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
    if (thread_.joinable()) thread_.join();
}

void DirectoryWatcher::ThreadMain() {
    alignas(FILE_NOTIFY_INFORMATION) char buffer[64 * 1024];
    while (running_.load()) {
        DWORD bytes = 0;
        if (!ReadDirectoryChangesW(handle_, buffer, sizeof(buffer), FALSE,
                                   FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_CREATION,
                                   &bytes, nullptr, nullptr)) {
            break;
        }
        if (callback_) callback_();
    }
}

} // namespace desktopsticker
