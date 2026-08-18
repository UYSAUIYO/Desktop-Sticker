#pragma once
#include <windows.h>
#include <d2d1.h>
#include <functional>
#include <string>
#include <vector>

#include "desktopsticker/ZoneModel.h"

namespace desktopsticker {

class IconService;

class ZoneWindow {
public:
    static bool RegisterClass(HINSTANCE hInst);
    static void UnregisterClass(HINSTANCE hInst);
    static ZoneWindow* FromHwnd(HWND hwnd);

    ZoneWindow(HINSTANCE hInst, const Zone& zone, IconService* icons);
    ~ZoneWindow();

    bool Create();
    void Destroy();

    HWND Hwnd() const { return hwnd_; }
    const Zone& GetZone() const { return zone_; }
    void SetZone(const Zone& zone);
    void Refresh();

    std::wstring HitTestItem(int x, int y) const;

    // 供 DesktopWorkspace 调用的交互回调
    std::function<void(const std::wstring& zoneId)> onCollapseToggle;
    std::function<void(const std::wstring& zoneId, const std::wstring& itemPath)> onItemDrag;
    std::function<void(const std::wstring& zoneId, const std::wstring& itemPath)> onRemoveItem;
    std::function<void(const std::wstring& zoneId)> onRenameZone;
    std::function<void(const std::wstring& zoneId)> onDeleteZone;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void OnPaint();
    void OnLButtonDown(int x, int y);
    void OnLButtonUp(int x, int y);
    void OnMouseMove(int x, int y);
    void OnRButtonUp(int x, int y);

    bool EnsureD2DResources();
    void ReleaseD2DResources();

    HINSTANCE hInst_ = nullptr;
    HWND hwnd_ = nullptr;
    Zone zone_;
    IconService* icons_ = nullptr;

    ID2D1Factory* factory_ = nullptr;
    ID2D1HwndRenderTarget* target_ = nullptr;
    ID2D1SolidColorBrush* bgBrush_ = nullptr;
    ID2D1SolidColorBrush* titleBrush_ = nullptr;
    IDWriteFactory* dwriteFactory_ = nullptr;
    IDWriteTextFormat* textFormat_ = nullptr;

    bool dragging_ = false;
    std::wstring draggingItem_;
    POINT dragStart_{};
    RECT windowStart_{};
};

} // namespace desktopsticker
