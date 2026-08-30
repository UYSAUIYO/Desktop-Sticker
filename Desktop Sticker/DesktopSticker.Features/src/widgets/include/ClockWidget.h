#pragma once
#include <string>
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>

#include "desktopsticker/Export.h"

namespace desktopsticker {

// 时钟文案（纯函数，可单测）
struct DESKTOPSTICKER_API ClockText {
    static std::wstring TimeText(const SYSTEMTIME& st); // HH:MM（补零）
    static std::wstring DateText(const SYSTEMTIME& st); // 2026年8月30日 周日
};

// 桌面时钟小组件：ULW 分层子窗口（与 ZoneWindow 同一渲染管线，本机唯一可见路径），
// 点击穿透（WS_EX_TRANSPARENT，不拦截磁贴交互与桌面空白双击），每秒检查、分钟变化才重绘。
// 由 DesktopWorkspace 负责创建、嵌入桌面与显隐。
class DESKTOPSTICKER_API ClockWidget {
public:
    static bool RegisterClass(HINSTANCE hInst);

    explicit ClockWidget(HINSTANCE hInst);
    ~ClockWidget();

    bool Create();
    void Destroy();
    // 强制下一帧重绘（嵌入完成/窗口状态变化后必须调用，分层表面会被 SetParent 重置）
    void Refresh();

    HWND Hwnd() const { return hwnd_; }
    int Width() const { return 240; }
    int Height() const { return 104; }
    void SetEmbedded(bool embedded) { embedded_ = embedded; }
    bool IsEmbedded() const { return embedded_; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void OnTick();
    void OnPaint();
    bool EnsureD2D();
    void ReleaseD2D();

    HINSTANCE hInst_ = nullptr;
    HWND hwnd_ = nullptr;
    bool embedded_ = false;
    bool painted_ = false;
    std::wstring timeText_;
    std::wstring dateText_;

    ID2D1Factory* factory_ = nullptr;
    ID2D1DCRenderTarget* target_ = nullptr;
    ID2D1SolidColorBrush* bgBrush_ = nullptr;
    ID2D1SolidColorBrush* borderBrush_ = nullptr;
    ID2D1SolidColorBrush* timeBrush_ = nullptr;
    ID2D1SolidColorBrush* dateBrush_ = nullptr;
    IDWriteFactory* dwriteFactory_ = nullptr;
    IDWriteTextFormat* timeFormat_ = nullptr;
    IDWriteTextFormat* dateFormat_ = nullptr;

    // 复用的 ULW 绘制 DIB（同 ZoneWindow）
    HBITMAP paintBmp_ = nullptr;
    void* paintBits_ = nullptr;
    int paintW_ = 0;
    int paintH_ = 0;
};

} // namespace desktopsticker
