#pragma once
#include <windows.h>
#include <d2d1.h>
#include <wincodec.h>
#include <functional>
#include <map>
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

    ZoneWindow(HINSTANCE hInst, const Zone& zone, IconService* icons,
               int columnSpacing = 48, int rowSpacing = 72);
    ~ZoneWindow();

    bool Create();
    void Destroy();

    HWND Hwnd() const { return hwnd_; }
    const Zone& GetZone() const { return zone_; }
    void SetZone(const Zone& zone);
    void SetSpacing(int columnSpacing, int rowSpacing);
    void Refresh();
    void SetEmbedded(bool embedded) { embedded_ = embedded; }
    bool IsEmbedded() const { return embedded_; }

    std::wstring HitTestItem(int x, int y) const;

    // 供 DesktopWorkspace 调用的交互回调
    std::function<void(const std::wstring& zoneId)> onCollapseToggle;
    // 磁贴拖放：松手时调用，screenPt 用于落点判定（拖到其他分区 / 拖出恢复桌面图标）
    std::function<void(const std::wstring& zoneId, const std::wstring& itemPath, POINT screenPt)> onTileDrop;
    std::function<void(const std::wstring& zoneId, const std::wstring& itemPath)> onRemoveItem;
    std::function<void(const std::wstring& zoneId)> onRenameZone;
    std::function<void(const std::wstring& zoneId)> onDeleteZone;
    // 卡片被拖动/缩放结束（rect 为屏幕坐标），用于持久化布局
    std::function<void(const std::wstring& zoneId, const RECT& rect)> onGeometryChanged;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void OnPaint();
    void OnLButtonDown(int x, int y);
    void OnLButtonUp(int x, int y);
    void OnMouseMove(int x, int y);
    void OnRButtonUp(int x, int y);
    void OnMouseWheel(int delta);
    void StartResize(int hitCode);

    bool EnsureD2DResources();
    void ReleaseD2DResources();
    void RenderTick();
    void RenderContentCache(int width, int contentHeight);

    HINSTANCE hInst_ = nullptr;
    HWND hwnd_ = nullptr;
    Zone zone_;
    IconService* icons_ = nullptr;
    bool embedded_ = false;

    ID2D1Factory* factory_ = nullptr;
    // 渲染到内存 DIB 再 UpdateLayeredWindow 合成；本机子窗口的“常量 Alpha + 窗口表面”
    // 路径不被 DWM 合成（实测 GDI/D2D 直绘均不可见），必须走 ULW
    ID2D1DCRenderTarget* target_ = nullptr;
    ID2D1SolidColorBrush* bgBrush_ = nullptr;
    ID2D1SolidColorBrush* titleBrush_ = nullptr;
    ID2D1SolidColorBrush* hoverBrush_ = nullptr;
    IDWriteFactory* dwriteFactory_ = nullptr;
    IDWriteTextFormat* textFormat_ = nullptr;
    IDWriteTextFormat* labelFormat_ = nullptr;
    IWICImagingFactory* wicFactory_ = nullptr;
    // 每个路径的 D2D 位图缓存（HICON 由 IconService 常驻缓存，可稳定复用）
    std::map<std::wstring, ID2D1Bitmap*> bitmapCache_;

    bool dragging_ = false;
    bool tileDragging_ = false;
    bool resizing_ = false;
    int resizeHit_ = 0;
    bool dragMoved_ = false;
    bool captureSet_ = false;
    bool geometryDirty_ = false;
    int scrollOffset_ = 0;
    int scrollTarget_ = 0;   // 平滑滚动目标位置（滚轮只改目标，由渲染定时器插值逼近）
    int wheelRemainder_ = 0; // 高分辨率滚轮的未满一格增量累积
    // 统一渲染定时器：所有重绘（滚动/hover/数据变化）合并钳制在 ~60fps，消除重绘堆积
    bool refreshPending_ = false;
    UINT_PTR renderTimer_ = 0;
    // 内容层缓存：磁贴内容渲染进中间位图，滚动帧只做一次 DrawBitmap 平移
    ID2D1BitmapRenderTarget* contentRt_ = nullptr;
    ID2D1Bitmap* contentBmp_ = nullptr;
    int contentW_ = 0;
    int contentH_ = 0;
    bool contentDirty_ = true;
    std::map<std::wstring, IDWriteTextLayout*> textLayoutCache_; // 名称布局只建一次
    // 复用的 ULW 绘制 DIB（避免每帧 CreateDIBSection 造成滚动卡顿）
    HBITMAP paintBmp_ = nullptr;
    void* paintBits_ = nullptr;
    int paintW_ = 0;
    int paintH_ = 0;
    int columnSpacing_ = 48;
    int rowSpacing_ = 72;
    std::wstring draggingItem_;
    std::wstring hoverItem_;
    std::wstring pressedItem_;
    POINT dragStart_{};
    RECT windowStart_{};
};

} // namespace desktopsticker
