#pragma once
#include <map>
#include <string>
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>

#include "desktopsticker/Export.h"
#include "WeatherService.h"

namespace desktopsticker {

// 时钟文案（纯函数，可单测）
struct DESKTOPSTICKER_API ClockText {
    static std::wstring TimeText(const SYSTEMTIME& st);     // HH:MM（补零）
    static std::wstring TimeSecText(const SYSTEMTIME& st);  // HH:MM:SS（补零）
    static std::wstring DateText(const SYSTEMTIME& st);     // 2026年8月30日 周日
    static std::wstring WeatherDesc(int code);              // WMO 天气代码 → 中文
    static std::wstring WindText(int deg, double mps);      // 风向度数+风速 m/s → 西北风 3级
    static int MinutesOfDay(const std::wstring& hhmm);      // "06:32" → 392；非法 -1
    // WMO 代码 → QWeather 图标代码（S2 图标集文件名，assets/weather/S2/<code>.png）
    static int QWeatherIconCode(int wmo, bool night);
};

// 桌面时钟小组件：ULW 分层子窗口（与 ZoneWindow 同一渲染管线）。
// 布局参照用户参考图：左侧模拟表盘（秒针平滑扫动），右侧大号数字时间（秒）+ 地区，
// 底部日出日落卡（太阳沿弧线动画）与当前小时天气卡。天气/定位由 WeatherService 后台提供。
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
    int Width() const { return 540; }
    int Height() const { return 300; }
    void SetEmbedded(bool embedded) { embedded_ = embedded; }
    bool IsEmbedded() const { return embedded_; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void OnTick();
    void OnPaint();
    bool EnsureD2D();
    void ReleaseD2D();
    // QWeather S2 图标位图（按代码懒加载并缓存；文件缺失时返回 nullptr 走手绘兜底）
    ID2D1Bitmap* IconBitmap(int qcode);
    // 天气图标：晴/多云/雨/雪/雷 按代码绘制（S2 图标缺失时的兜底）
    void DrawWeatherIcon(int code, float cx, float cy);

    HINSTANCE hInst_ = nullptr;
    HWND hwnd_ = nullptr;
    bool embedded_ = false;
    bool painted_ = false;
    std::wstring timeText_;   // HH:MM:SS
    std::wstring dateText_;
    std::wstring locText_;    // 国家 · 省 · 市
    std::wstring sunText_;    // 日出时刻 HH:MM
    std::wstring setText_;    // 日落时刻 HH:MM
    std::wstring tempText_;   // 26°
    std::wstring descText_;   // 晴 · 西北风 3级
    int weatherCode_ = -1;
    int sunriseMin_ = -1;
    int sunsetMin_ = -1;
    int nowMin_ = 0;
    int iconCode_ = 999; // QWeather S2 图标代码

    std::map<int, ID2D1Bitmap*> iconBitmaps_; // QWeather S2 图标缓存（代码 → 位图）
    IWICImagingFactory* wicFactory_ = nullptr;

    WeatherService weather_;

    ID2D1Factory* factory_ = nullptr;
    ID2D1DCRenderTarget* target_ = nullptr;
    ID2D1SolidColorBrush* bgBrush_ = nullptr;
    ID2D1SolidColorBrush* borderBrush_ = nullptr;
    ID2D1SolidColorBrush* handBrush_ = nullptr;
    ID2D1SolidColorBrush* subBrush_ = nullptr;
    ID2D1SolidColorBrush* orangeBrush_ = nullptr;
    ID2D1SolidColorBrush* cardBrush_ = nullptr;
    ID2D1GradientStopCollection* timeStops_ = nullptr;
    ID2D1LinearGradientBrush* timeGradBrush_ = nullptr;
    ID2D1GradientStopCollection* sunCoreStops_ = nullptr;
    ID2D1RadialGradientBrush* sunCoreBrush_ = nullptr;
    ID2D1GradientStopCollection* sunGlowStops_ = nullptr;
    ID2D1RadialGradientBrush* sunGlowBrush_ = nullptr;
    IDWriteFactory* dwriteFactory_ = nullptr;
    IDWriteTextFormat* fmtTime_ = nullptr;
    IDWriteTextFormat* fmtSub_ = nullptr;
    IDWriteTextFormat* fmtLoc_ = nullptr;
    IDWriteTextFormat* fmtNum_ = nullptr;
    IDWriteTextFormat* fmtCard_ = nullptr;
    IDWriteTextFormat* fmtVal_ = nullptr;
    IDWriteTextFormat* fmtTemp_ = nullptr;
    IDWriteTextFormat* fmtDesc_ = nullptr;

    // 复用的 ULW 绘制 DIB（同 ZoneWindow）
    HBITMAP paintBmp_ = nullptr;
    void* paintBits_ = nullptr;
    int paintW_ = 0;
    int paintH_ = 0;
};

} // namespace desktopsticker
