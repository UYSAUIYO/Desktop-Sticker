#pragma once
#include <string>
#include <windows.h>

namespace desktopsticker {

// 简易模态输入框（分区重命名用）。阻塞运行本地消息循环直至确定/取消，
// 返回是否确定；确定时 value 带回输入内容。
bool ShowRenameDialog(HWND owner, const std::wstring& title, std::wstring& value);

} // namespace desktopsticker
