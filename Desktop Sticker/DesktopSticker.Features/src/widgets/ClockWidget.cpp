#include "pch.h"
#include "ClockWidget.h"

#include <cwchar>
#include <dwrite.h>

namespace desktopsticker {

namespace {
const wchar_t kClockWindowClass[] = L"DesktopSticker.ClockWindow";
constexpr UINT_PTR kClockTimerId = 1;
constexpr float kPi = 3.14159265f;

// WMO 天气代码族：0 晴 1 云 2 雾 3 雨 4 雪 5 雷
int WeatherFamily(int code) {
    if (code < 0) return -1;
    if (code <= 1) return 0;
    if (code <= 3) return 1;
    if (code <= 48) return 2;
    if (code <= 67 || (code >= 80 && code <= 82)) return 3;
    if (code <= 77 || code == 85 || code == 86) return 4;
    return 5;
}
} // namespace

std::wstring ClockText::TimeText(const SYSTEMTIME& st) {
    wchar_t buf[16]{};
    swprintf_s(buf, L"%02d:%02d", st.wHour, st.wMinute);
    return buf;
}

std::wstring ClockText::TimeSecText(const SYSTEMTIME& st) {
    wchar_t buf[16]{};
    swprintf_s(buf, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    return buf;
}

std::wstring ClockText::DateText(const SYSTEMTIME& st) {
    static const wchar_t* kWeekday[] = {L"周日", L"周一", L"周二", L"周三", L"周四", L"周五", L"周六"};
    wchar_t buf[64]{};
    swprintf_s(buf, L"%d年%d月%d日 %s", st.wYear, st.wMonth, st.wDay, kWeekday[st.wDayOfWeek % 7]);
    return buf;
}

std::wstring ClockText::WeatherDesc(int code) {
    switch (code) {
    case 0: return L"晴";
    case 1: case 2: case 3: return L"多云";
    case 45: case 48: return L"雾";
    case 51: case 53: case 55: return L"毛毛雨";
    case 56: case 57: return L"冻毛毛雨";
    case 61: case 63: case 65: return L"雨";
    case 66: case 67: return L"冻雨";
    case 71: case 73: case 75: return L"雪";
    case 77: return L"雪粒";
    case 80: case 81: case 82: return L"阵雨";
    case 85: case 86: return L"阵雪";
    case 95: return L"雷雨";
    case 96: case 99: return L"雷雨伴冰雹";
    default: return L"未知";
    }
}

std::wstring ClockText::WindText(int deg, double mps) {
    static const wchar_t* kDir[] = {L"北", L"东北", L"东", L"东南", L"南", L"西南", L"西", L"西北"};
    static const double kLevel[] = {0.3, 1.6, 3.4, 5.5, 8.0, 10.8, 13.9, 17.2, 20.8, 24.5, 28.5, 32.7};
    int level = 0;
    for (double t : kLevel) {
        if (mps >= t) ++level;
        else break;
    }
    if (level == 0) return L"无风";
    wchar_t buf[32]{};
    swprintf_s(buf, L"%s风 %d级", kDir[((deg % 360) + 360 + 22) % 360 / 45], level);
    return buf;
}

int ClockText::MinutesOfDay(const std::wstring& hhmm) {
    const size_t colon = hhmm.find(L':');
    if (colon == std::wstring::npos || colon == 0 || colon + 1 >= hhmm.size()) return -1;
    const int h = _wtoi(hhmm.substr(0, colon).c_str());
    const int m = _wtoi(hhmm.substr(colon + 1).c_str());
    if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
    return h * 60 + m;
}

int ClockText::QWeatherIconCode(int wmo, bool night) {
    int day;
    switch (wmo) {
    case 0: day = 100; break;           // 晴
    case 1: day = 102; break;           // 少云
    case 2: day = 101; break;           // 多云
    case 3: day = 104; break;           // 阴
    case 45: case 48: day = 501; break; // 雾
    case 51: case 53: case 55: day = 309; break; // 毛毛雨
    case 56: case 57: case 66: case 67: day = 313; break; // 冻雨
    case 61: day = 305; break;          // 小雨
    case 63: day = 306; break;          // 中雨
    case 65: day = 307; break;          // 大雨
    case 71: day = 400; break;          // 小雪
    case 73: day = 401; break;          // 中雪
    case 75: day = 402; break;          // 大雪
    case 77: day = 408; break;          // 雪粒
    case 80: day = 300; break;          // 阵雨
    case 81: day = 301; break;          // 强阵雨
    case 82: day = 302; break;          // 强雷阵雨
    case 85: case 86: day = 407; break; // 阵雪
    case 95: day = 302; break;          // 雷阵雨
    case 96: case 99: day = 304; break; // 雷阵雨伴冰雹
    default: day = 999; break;          // 未知
    }
    if (!night) return day;
    // 夜间变体（S2 图标集提供的夜间代码）
    switch (day) {
    case 100: return 150;
    case 101: case 102: return 153; // 少云/多云（夜）
    case 104: return 154;           // 阴（夜）
    case 300: return 350;           // 阵雨（夜）
    case 301: return 351;           // 强阵雨（夜）
    case 407: return 456;           // 阵雪（夜）
    case 408: return 457;           // 雪粒（夜）
    default: return day;            // 雨/雪/雾等昼夜同图
    }
}

bool ClockWidget::RegisterClass(HINSTANCE hInst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClockWindowClass;
    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

ClockWidget::ClockWidget(HINSTANCE hInst) : hInst_(hInst) {}

ClockWidget::~ClockWidget() {
    Destroy();
    ReleaseD2D();
}

bool ClockWidget::Create() {
    if (hwnd_) return true;
    // 与磁贴同款分层子窗口：外观由 ULW 逐像素 alpha 决定（禁用 SLWA）
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                            kClockWindowClass, L"",
                            WS_POPUP | WS_VISIBLE,
                            100, 100, Width(), Height(),
                            nullptr, nullptr, hInst_, this);
    if (!hwnd_) return false;
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    // 100ms：秒针平滑扫动 + 太阳光晕呼吸都靠这帧率
    SetTimer(hwnd_, kClockTimerId, 100, nullptr);
    weather_.Start();
    return true;
}

void ClockWidget::Destroy() {
    if (hwnd_ && IsWindow(hwnd_)) {
        KillTimer(hwnd_, kClockTimerId);
        DestroyWindow(hwnd_);
    }
    hwnd_ = nullptr;
    weather_.Stop();
    if (paintBmp_) {
        DeleteObject(paintBmp_);
        paintBmp_ = nullptr;
        paintBits_ = nullptr;
    }
    paintW_ = paintH_ = 0;
}

void ClockWidget::Refresh() {
    painted_ = false; // 强制重绘（SetParent/样式切换会重置分层表面）
    OnTick();
}

void ClockWidget::OnTick() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    timeText_ = ClockText::TimeSecText(st);
    dateText_ = ClockText::DateText(st);

    const WeatherInfo wx = weather_.Snapshot();
    locText_ = wx.located ? wx.country : L"本地时间";
    if (wx.located) {
        if (!wx.region.empty() && wx.region != locText_) locText_ += L" · " + wx.region;
        if (!wx.city.empty() && wx.city != wx.region) locText_ += L" · " + wx.city;
    }
    if (wx.valid) {
        sunText_ = wx.sunrise;
        setText_ = wx.sunset;
        wchar_t buf[16]{};
        swprintf_s(buf, L"%.0f°", wx.temp);
        tempText_ = buf;
        descText_ = ClockText::WeatherDesc(wx.weatherCode) + L" · " +
                    ClockText::WindText(wx.windDir, wx.windSpeed);
        weatherCode_ = wx.weatherCode;
        sunriseMin_ = wx.sunriseMin;
        sunsetMin_ = wx.sunsetMin;
    } else {
        sunText_ = L"日出 --:--";
        setText_ = L"日落 --:--";
        tempText_ = L"--°";
        descText_ = L"天气获取中…";
        weatherCode_ = -1;
        sunriseMin_ = sunsetMin_ = -1;
    }
    nowMin_ = st.wHour * 60 + st.wMinute;

    // 图标代码：按日出日落判断昼夜，映射到 QWeather S2 图标
    const bool night = (sunriseMin_ >= 0 && sunsetMin_ > sunriseMin_)
                           ? (nowMin_ < sunriseMin_ || nowMin_ >= sunsetMin_)
                           : (st.wHour >= 19 || st.wHour < 6);
    iconCode_ = ClockText::QWeatherIconCode(weatherCode_, night);

    OnPaint(); // 常驻重绘：秒针扫动与太阳光晕呼吸
}

bool ClockWidget::EnsureD2D() {
    if (factory_ && dwriteFactory_ && target_) return true;
    if (!factory_) {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory_);
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown**>(&dwriteFactory_));
        if (!factory_ || !dwriteFactory_) return false;
    }
    if (!target_) {
        const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        factory_->CreateDCRenderTarget(&props, &target_);
        if (!target_) return false;
    }
    if (!bgBrush_) target_->CreateSolidColorBrush(D2D1::ColorF(0x25272E, 0.78f), &bgBrush_);
    if (!borderBrush_) target_->CreateSolidColorBrush(D2D1::ColorF(0xFFFFFF, 0.12f), &borderBrush_);
    if (!cardBrush_) target_->CreateSolidColorBrush(D2D1::ColorF(0xFFFFFF, 0.06f), &cardBrush_);
    if (!handBrush_) target_->CreateSolidColorBrush(D2D1::ColorF(0xFFFFFF, 0.92f), &handBrush_);
    if (!subBrush_) target_->CreateSolidColorBrush(D2D1::ColorF(0xFFFFFF, 0.55f), &subBrush_);
    if (!orangeBrush_) target_->CreateSolidColorBrush(D2D1::ColorF(0xFFA43C, 0.95f), &orangeBrush_);
    if (timeStops_ == nullptr) {
        const D2D1_GRADIENT_STOP stops[] = {
            {0.0f, D2D1::ColorF(0xFFC26E)}, {0.55f, D2D1::ColorF(0xFFA43C)}, {1.0f, D2D1::ColorF(0xF07E1A)}};
        target_->CreateGradientStopCollection(stops, 3, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &timeStops_);
        if (timeStops_) {
            target_->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(1, 0)),
                timeStops_, &timeGradBrush_);
        }
    }
    if (sunCoreStops_ == nullptr) {
        // 边缘减光（limb darkening）：边缘更暗更饱和 → 球体立体感
        const D2D1_GRADIENT_STOP stops[] = {
            {0.0f, D2D1::ColorF(0xFFF9E3)}, {0.5f, D2D1::ColorF(0xFFD066)},
            {0.8f, D2D1::ColorF(0xFF9F3A)}, {1.0f, D2D1::ColorF(0xF0731C)}};
        target_->CreateGradientStopCollection(stops, 4, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &sunCoreStops_);
    if (sunCoreStops_) {
        // 径向刷半径只能在创建时给定；位置/缩放由画刷变换在绘制时设置。
        // 四段色 stop：中心亮黄 → 橙 → 深橙，边缘略暗（limb darkening，立体感）
        target_->CreateRadialGradientBrush(
            D2D1::RadialGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, -3), 19, 19),
            sunCoreStops_, &sunCoreBrush_);
    }
    }
    if (sunGlowStops_ == nullptr) {
        const D2D1_GRADIENT_STOP stops[] = {
            {0.0f, D2D1::ColorF(0xFFA43C, 0.45f)}, {1.0f, D2D1::ColorF(0xFFA43C, 0.0f)}};
        target_->CreateGradientStopCollection(stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &sunGlowStops_);
        if (sunGlowStops_) {
            target_->CreateRadialGradientBrush(
                D2D1::RadialGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, 0), 33, 33),
                sunGlowStops_, &sunGlowBrush_);
        }
    }
    if (ringBrush_ == nullptr) {
        target_->CreateSolidColorBrush(D2D1::ColorF(0xFFFFFF, 1.0f), &ringBrush_);
    }
    if (planetStops_ == nullptr) {
        // 地球受光渐变：亮面（昼）在画刷空间 +x 方向（绘制时旋转向太阳），背面深海军蓝
        const D2D1_GRADIENT_STOP stops[] = {
            {0.0f, D2D1::ColorF(0xE8F6FF)}, {0.35f, D2D1::ColorF(0x5EA0E8)},
            {0.7f, D2D1::ColorF(0x1E4FA8)}, {1.0f, D2D1::ColorF(0x0D1633)}};
        target_->CreateGradientStopCollection(stops, 4, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &planetStops_);
        if (planetStops_) {
            target_->CreateRadialGradientBrush(
                D2D1::RadialGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(3, 0), 6, 6),
                planetStops_, &planetBrush_);
        }
    }
    if (fmtTime_ == nullptr) {
        dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                         DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                         54.0f, L"zh-cn", &fmtTime_);
        if (fmtTime_) {
            fmtTime_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            fmtTime_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
    if (fmtSub_ == nullptr) {
        dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                         DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                         13.0f, L"zh-cn", &fmtSub_);
        if (fmtSub_) {
            fmtSub_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            fmtSub_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
    if (fmtLoc_ == nullptr) {
        dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                         DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                         15.0f, L"zh-cn", &fmtLoc_);
        if (fmtLoc_) {
            fmtLoc_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            fmtLoc_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
    if (fmtNum_ == nullptr) {
        dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                         DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                         15.0f, L"zh-cn", &fmtNum_);
        if (fmtNum_) {
            fmtNum_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            fmtNum_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
    if (fmtCard_ == nullptr) {
        dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                         DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                         14.0f, L"zh-cn", &fmtCard_);
    }
    if (fmtVal_ == nullptr) {
        dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                         DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                         16.0f, L"zh-cn", &fmtVal_);
    }
    if (fmtTemp_ == nullptr) {
        dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                         DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                         30.0f, L"zh-cn", &fmtTemp_);
    }
    if (fmtDesc_ == nullptr) {
        dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                         DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                         12.0f, L"zh-cn", &fmtDesc_);
    }
    return bgBrush_ && borderBrush_ && cardBrush_ && handBrush_ && subBrush_ && orangeBrush_ &&
           ringBrush_ && planetBrush_ &&
           timeGradBrush_ && sunCoreBrush_ && sunGlowBrush_ &&
           fmtTime_ && fmtSub_ && fmtLoc_ && fmtNum_ && fmtCard_ && fmtVal_ && fmtTemp_ && fmtDesc_;
}

void ClockWidget::ReleaseD2D() {
    if (fmtTime_) fmtTime_->Release();
    if (fmtSub_) fmtSub_->Release();
    if (fmtLoc_) fmtLoc_->Release();
    if (fmtNum_) fmtNum_->Release();
    if (fmtCard_) fmtCard_->Release();
    if (fmtVal_) fmtVal_->Release();
    if (fmtTemp_) fmtTemp_->Release();
    if (fmtDesc_) fmtDesc_->Release();
    if (timeGradBrush_) timeGradBrush_->Release();
    if (timeStops_) timeStops_->Release();
    if (sunCoreBrush_) sunCoreBrush_->Release();
    if (sunCoreStops_) sunCoreStops_->Release();
    if (sunGlowBrush_) sunGlowBrush_->Release();
    if (sunGlowStops_) sunGlowStops_->Release();
    if (ringBrush_) ringBrush_->Release();
    if (planetBrush_) planetBrush_->Release();
    if (planetStops_) planetStops_->Release();
    if (borderBrush_) borderBrush_->Release();
    if (bgBrush_) bgBrush_->Release();
    if (cardBrush_) cardBrush_->Release();
    if (handBrush_) handBrush_->Release();
    if (subBrush_) subBrush_->Release();
    if (orangeBrush_) orangeBrush_->Release();
    if (target_) target_->Release();
    if (dwriteFactory_) dwriteFactory_->Release();
    if (factory_) factory_->Release();
    for (auto& [code, bmp] : iconBitmaps_) {
        if (bmp) bmp->Release();
    }
    iconBitmaps_.clear();
    if (wicFactory_) wicFactory_->Release();
    wicFactory_ = nullptr;
    fmtTime_ = nullptr; fmtSub_ = nullptr; fmtLoc_ = nullptr; fmtNum_ = nullptr;
    fmtCard_ = nullptr; fmtTemp_ = nullptr; fmtDesc_ = nullptr;
    timeGradBrush_ = nullptr; timeStops_ = nullptr;
    sunCoreBrush_ = nullptr; sunCoreStops_ = nullptr;
    sunGlowBrush_ = nullptr; sunGlowStops_ = nullptr;
    ringBrush_ = nullptr; planetBrush_ = nullptr; planetStops_ = nullptr;
    borderBrush_ = nullptr; bgBrush_ = nullptr; cardBrush_ = nullptr;
    handBrush_ = nullptr; subBrush_ = nullptr; orangeBrush_ = nullptr;
    target_ = nullptr; dwriteFactory_ = nullptr; factory_ = nullptr;
}

void ClockWidget::DrawWeatherIcon(int code, float cx, float cy) {
    const int fam = WeatherFamily(code);
    // 云朵底（雨/雪/雷/雾与多云共用）
    auto cloud = [&]() {
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - 8, cy - 6), 12, 10), handBrush_);
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + 6, cy - 4), 10, 8), handBrush_);
        target_->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(cx - 19, cy - 4, cx + 14, cy + 10), 8, 8), handBrush_);
    };
    if (fam == 0) { // 晴：太阳 + 光芒
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 10, 10), orangeBrush_);
        for (int i = 0; i < 8; ++i) {
            const float a = i * kPi / 4.0f;
            const float c = cosf(a), s = sinf(a);
            target_->DrawLine(D2D1::Point2F(cx + c * 14, cy + s * 14),
                              D2D1::Point2F(cx + c * 20, cy + s * 20), orangeBrush_, 2.0f);
        }
        return;
    }
    if (fam == 1) { // 多云：小太阳 + 云
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + 11, cy - 12), 6, 6), orangeBrush_);
        cloud();
        return;
    }
    cloud();
    if (fam == 3) { // 雨
        for (int i = 0; i < 3; ++i) {
            const float x = cx - 12 + i * 10;
            target_->DrawLine(D2D1::Point2F(x, cy + 13), D2D1::Point2F(x - 3, cy + 21), subBrush_, 2.0f);
        }
    } else if (fam == 4) { // 雪
        for (int i = 0; i < 3; ++i) {
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - 12 + i * 10, cy + 16), 2, 2), handBrush_);
        }
    } else if (fam == 5) { // 雷
        target_->DrawLine(D2D1::Point2F(cx + 2, cy + 12), D2D1::Point2F(cx - 4, cy + 18), orangeBrush_, 2.5f);
        target_->DrawLine(D2D1::Point2F(cx - 4, cy + 18), D2D1::Point2F(cx + 3, cy + 22), orangeBrush_, 2.5f);
    }
}

ID2D1Bitmap* ClockWidget::IconBitmap(int qcode) {
    auto it = iconBitmaps_.find(qcode);
    if (it != iconBitmaps_.end()) return it->second;
    if (!wicFactory_) {
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(&wicFactory_));
        if (!wicFactory_) return nullptr;
    }
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::filesystem::path dir =
        std::filesystem::path(exe).parent_path() / L"assets" / L"weather" / L"S2";

    // 精确代码 → 999 兜底；加载结果（含兜底命中）都缓存
    for (int attempt : {qcode, 999}) {
        const std::filesystem::path file = dir / (std::to_wstring(attempt) + L".png");
        if (!std::filesystem::exists(file)) continue;
        ID2D1Bitmap* bmp = nullptr;
        IWICBitmapDecoder* dec = nullptr;
        if (SUCCEEDED(wicFactory_->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ,
                                                             WICDecodeMetadataCacheOnDemand, &dec))) {
            IWICBitmapFrameDecode* frame = nullptr;
            IWICFormatConverter* conv = nullptr;
            if (SUCCEEDED(dec->GetFrame(0, &frame)) &&
                SUCCEEDED(wicFactory_->CreateFormatConverter(&conv)) &&
                SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
                                           WICBitmapDitherTypeNone, nullptr, 0.0,
                                           WICBitmapPaletteTypeCustom)) &&
                SUCCEEDED(target_->CreateBitmapFromWicBitmap(conv, nullptr, &bmp))) {
                iconBitmaps_[qcode] = bmp;
                if (attempt == 999 && qcode != 999) iconBitmaps_[999] = bmp;
            }
            if (conv) conv->Release();
            if (frame) frame->Release();
            dec->Release();
        }
        if (bmp) return bmp;
    }
    return nullptr; // 交给手绘兜底
}

void ClockWidget::OnPaint() {
    if (!hwnd_ || !IsWindow(hwnd_)) return;
    if (!EnsureD2D()) return;

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    if (!paintBmp_ || paintW_ != w || paintH_ != h) {
        if (paintBmp_) DeleteObject(paintBmp_);
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h; // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        paintBmp_ = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &paintBits_, nullptr, 0);
        if (!paintBmp_) {
            DeleteDC(hdcMem);
            ReleaseDC(nullptr, hdcScreen);
            return;
        }
        paintW_ = w;
        paintH_ = h;
    }
    HGDIOBJ oldBmp = SelectObject(hdcMem, paintBmp_);

    target_->BindDC(hdcMem, &rc);
    target_->BeginDraw();
    target_->Clear(D2D1::ColorF(0, 0));

    const float fw = static_cast<float>(w);
    const float fh = static_cast<float>(h);
    target_->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(1, 1, fw - 1, fh - 1), 8, 8), bgBrush_);
    target_->DrawRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, fw - 0.5f, fh - 0.5f), 8, 8), borderBrush_, 1.0f);

    // —— 左：模拟表盘（基准半径 96 的 0.85 倍） ——
    const float cx = 148.0f, cy = 100.0f, faceR = 80.0f;
    target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), faceR, faceR), borderBrush_, 1.0f);
    for (int i = 0; i < 60; ++i) {
        const float a = i * 6.0f * kPi / 180.0f;
        const bool major = i % 5 == 0;
        const float len = major ? 10.0f : 4.5f;
        const float r0 = faceR - 4.0f;
        target_->DrawLine(D2D1::Point2F(cx + sinf(a) * r0, cy - cosf(a) * r0),
                          D2D1::Point2F(cx + sinf(a) * (r0 - len), cy - cosf(a) * (r0 - len)),
                          major ? handBrush_ : subBrush_, major ? 2.5f : 1.0f);
    }
    wchar_t num[4]{};
    for (int n = 1; n <= 12; ++n) {
        const float a = n * 30.0f * kPi / 180.0f;
        swprintf_s(num, L"%d", n);
        target_->DrawTextW(num, static_cast<UINT32>(wcslen(num)), fmtNum_,
                           D2D1::RectF(cx + sinf(a) * 56 - 14, cy - cosf(a) * 56 - 11,
                                       cx + sinf(a) * 56 + 14, cy - cosf(a) * 56 + 11),
                           handBrush_);
    }

    SYSTEMTIME st;
    GetLocalTime(&st);
    const float ms = static_cast<float>(st.wMilliseconds);
    const float hourDeg = ((st.wHour % 12) + st.wMinute / 60.0f + st.wSecond / 3600.0f) * 30.0f;
    const float minDeg = (st.wMinute + st.wSecond / 60.0f) * 6.0f;
    const float secDeg = (st.wSecond + ms / 1000.0f) * 6.0f;
    auto hand = [&](float deg, float len, float width, ID2D1SolidColorBrush* b) {
        const float a = deg * kPi / 180.0f;
        target_->DrawLine(D2D1::Point2F(cx - sinf(a) * 10.0f, cy + cosf(a) * 10.0f),
                          D2D1::Point2F(cx + sinf(a) * len, cy - cosf(a) * len), b, width);
    };
    hand(hourDeg, 39, 4.5f, handBrush_);
    hand(minDeg, 60, 3.0f, handBrush_);
    hand(secDeg, 68, 1.5f, orangeBrush_);
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 4.5f, 4.5f), orangeBrush_);
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 2.0f, 2.0f), handBrush_);

    // —— 右：日期 + 数字时间 + 地区 ——
    target_->DrawTextW(dateText_.c_str(), static_cast<UINT32>(dateText_.size()), fmtSub_,
                       D2D1::RectF(260, 28, 520, 52), subBrush_);
    timeGradBrush_->SetStartPoint(D2D1::Point2F(270, 0));
    timeGradBrush_->SetEndPoint(D2D1::Point2F(515, 0));
    target_->DrawTextW(timeText_.c_str(), static_cast<UINT32>(timeText_.size()), fmtTime_,
                       D2D1::RectF(260, 52, 520, 128), timeGradBrush_);
    target_->DrawTextW(locText_.c_str(), static_cast<UINT32>(locText_.size()), fmtLoc_,
                       D2D1::RectF(260, 132, 520, 164), handBrush_);

    // —— 底部左：太阳-行星轨道（伪 3D：深度明暗/近大远小/绘制顺序遮挡）+ 日出日落 ——
    target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(24, 206, 322, 288), 8, 8), cardBrush_);

    const float phase = (st.wSecond * 1000.0f + ms) / 1000.0f; // 秒内相位（动画用）

    // 星空点缀（固定位置，缓慢闪烁）
    {
        static const float stars[][3] = {
            {46, 220, 1.0f}, {140, 215, 0.8f}, {64, 240, 0.6f}, {126, 233, 1.0f},
            {88, 217, 0.7f}, {152, 226, 0.9f}, {56, 268, 0.8f}, {140, 264, 1.0f}};
        for (int i = 0; i < 8; ++i) {
            const float tw = 0.5f + 0.5f * sinf(phase * 2.0f + i * 1.7f);
            ringBrush_->SetColor(D2D1::ColorF(0xFFFFFF, (0.10f + 0.22f * tw) * stars[i][2]));
            target_->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(stars[i][0], stars[i][1]), 1.2f, 1.2f), ringBrush_);
        }
    }

    const float sx = 104.0f, sy = 250.0f;
    const float orbRx = 56.0f, orbRy = 20.0f, orbRotDeg = -22.0f;
    const float orbRot = orbRotDeg * kPi / 180.0f;
    // 公转角 = 一天中的时刻锚定到轨道：亮弧（近侧）是白天——日出(左端)→正午(底)→日落(右端)；
    // 暗弧（远侧）是夜间——日落(右端)→午夜(顶)→日出(左端)。昼夜时长按真实日出日落数据分配
    float orbitU = static_cast<float>(nowMin_) / 1440.0f * 2.0f * kPi; // 无日出数据时的兜底
    if (sunriseMin_ >= 0 && sunsetMin_ > sunriseMin_) {
        if (nowMin_ >= sunriseMin_ && nowMin_ <= sunsetMin_) {
            const float t = static_cast<float>(nowMin_ - sunriseMin_) /
                            static_cast<float>(sunsetMin_ - sunriseMin_);
            orbitU = kPi * (1.0f - t); // 白天走亮弧：日出左端 → 正午底 → 日落右端
        } else {
            const int nightLen = 1440 - (sunsetMin_ - sunriseMin_);
            const int nowNight = nowMin_ >= sunsetMin_ ? nowMin_ - sunsetMin_
                                                       : nowMin_ + 1440 - sunsetMin_;
            orbitU = 2.0f * kPi - (static_cast<float>(nowNight) / nightLen) * kPi; // 夜走暗弧：日落右端 → 午夜顶 → 日出左端
        }
    }
    auto orbitPoint = [&](float u) {
        const float x0 = orbRx * cosf(u), y0 = orbRy * sinf(u);
        return D2D1_POINT_2F{sx + x0 * cosf(orbRot) - y0 * sinf(orbRot),
                             sy + x0 * sinf(orbRot) + y0 * cosf(orbRot)};
    };
    // 轨道弧段：u0→u1（屏幕顺时针 = 参数角递增），rot 为椭圆长轴旋转
    auto orbitArc = [&](float u0, float u1, float width, ID2D1SolidColorBrush* b) {
        ID2D1PathGeometry* geo = nullptr;
        if (SUCCEEDED(factory_->CreatePathGeometry(&geo))) {
            ID2D1GeometrySink* sink = nullptr;
            if (SUCCEEDED(geo->Open(&sink))) {
                sink->BeginFigure(orbitPoint(u0), D2D1_FIGURE_BEGIN_HOLLOW);
                D2D1_ARC_SEGMENT seg{};
                seg.point = orbitPoint(u1);
                seg.size = D2D1::SizeF(orbRx, orbRy);
                seg.rotationAngle = orbRotDeg;
                seg.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
                seg.arcSize = D2D1_ARC_SIZE_SMALL; // 短段必须 SMALL：LARGE 会画出近整椭圆（曾叠成螺旋）
                sink->AddArc(seg);
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                sink->Close();
                sink->Release();
                target_->DrawGeometry(geo, b, width);
            }
            geo->Release();
        }
    };
    // 行星 = 地球：近大远小 + 受光面朝向太阳（径向渐变随公转旋转）
    auto drawPlanet = [&]() {
        const D2D1_POINT_2F pp = orbitPoint(orbitU);
        const float depth = 0.5f + 0.5f * sinf(orbitU); // 0 远侧 … 1 近侧
        const float pr = 3.0f + 2.2f * depth;
        const float a = atan2f(sy - pp.y, sx - pp.x);
        planetBrush_->SetTransform(D2D1::Matrix3x2F::Scale(pr / 6.0f, pr / 6.0f) *
                                   D2D1::Matrix3x2F::Rotation(a * 180.0f / kPi) *
                                   D2D1::Matrix3x2F::Translation(pp.x, pp.y));
        target_->FillEllipse(D2D1::Ellipse(pp, pr, pr), planetBrush_);
        target_->DrawEllipse(D2D1::Ellipse(pp, pr + 0.6f, pr + 0.6f),
                             subBrush_, 0.8f); // 大气层薄边
    };

    // 远侧：暗环段 = 夜间（先画，经过太阳圆面的段落会被太阳自然遮挡）
    constexpr int kSegs = 48;
    const bool planetFront = sinf(orbitU) > 0; // 参数下半圈在椭圆近侧（前景）
    for (int i = kSegs / 2; i < kSegs; ++i) {
        const float u0 = i * 2.0f * kPi / kSegs;
        const float u1 = (i + 1) * 2.0f * kPi / kSegs;
        const float depth = 0.5f + 0.5f * sinf((u0 + u1) * 0.5f);
        ringBrush_->SetColor(D2D1::ColorF(0xFFFFFF, 0.15f + 0.30f * depth));
        orbitArc(u0, u1, 1.0f + 0.8f * depth, ringBrush_);
    }
    if (!planetFront) drawPlanet();

    // 太阳：外层光晕呼吸 + 径向渐变核球（径向刷半径固定，位置/缩放走画刷变换）
    const float glowR = 27.0f + sinf(phase * 2.2f) * 3.0f;
    const D2D1_MATRIX_3X2_F atSun =
        D2D1::Matrix3x2F::Scale(glowR / 33.0f, glowR / 33.0f) * D2D1::Matrix3x2F::Translation(sx, sy);
    sunGlowBrush_->SetTransform(atSun);
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(sx, sy), glowR, glowR), sunGlowBrush_);
    sunCoreBrush_->SetTransform(D2D1::Matrix3x2F::Translation(sx, sy));
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(sx, sy), 19, 19), sunCoreBrush_);
    target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(sx, sy), 18.6f, 18.6f), orangeBrush_, 1.1f);

    // 近侧：亮环段 = 白天（粗且亮，压在太阳前）+ 地球（白天走在亮弧上）
    for (int i = 0; i < kSegs / 2; ++i) {
        const float u0 = i * 2.0f * kPi / kSegs;
        const float u1 = (i + 1) * 2.0f * kPi / kSegs;
        const float depth = 0.5f + 0.5f * sinf((u0 + u1) * 0.5f);
        ringBrush_->SetColor(D2D1::ColorF(0xFFFFFF, 0.30f + 0.55f * depth));
        orbitArc(u0, u1, 1.4f + 1.4f * depth, ringBrush_);
    }
    if (planetFront) drawPlanet();

    // 右侧：日出 / 日落时刻（纵向两行）
    target_->DrawTextW(L"日出", 2, fmtCard_, D2D1::RectF(172, 216, 214, 240), subBrush_);
    target_->DrawTextW(sunText_.c_str(), static_cast<UINT32>(sunText_.size()), fmtVal_,
                       D2D1::RectF(214, 214, 312, 242), handBrush_);
    target_->DrawTextW(L"日落", 2, fmtCard_, D2D1::RectF(172, 252, 214, 276), subBrush_);
    target_->DrawTextW(setText_.c_str(), static_cast<UINT32>(setText_.size()), fmtVal_,
                       D2D1::RectF(214, 250, 312, 278), handBrush_);

    // —— 底部右：当前小时天气 ——
    target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(338, 206, 516, 288), 8, 8), cardBrush_);
    target_->DrawTextW(L"当前小时", 4, fmtDesc_,
                       D2D1::RectF(350, 214, 440, 234), subBrush_);
    target_->DrawTextW(tempText_.c_str(), static_cast<UINT32>(tempText_.size()), fmtTemp_,
                       D2D1::RectF(350, 234, 436, 274), handBrush_);
    target_->DrawTextW(descText_.c_str(), static_cast<UINT32>(descText_.size()), fmtDesc_,
                       D2D1::RectF(350, 266, 504, 286), subBrush_);
    // QWeather S2 彩色图标（54px 基准 ×1.15）；文件缺失时回退手绘
    if (ID2D1Bitmap* bmp = IconBitmap(iconCode_)) {
        target_->DrawBitmap(bmp, D2D1::RectF(440, 218, 502, 280), 1.0f,
                            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else {
        DrawWeatherIcon(weatherCode_, 464, 244);
    }

    const HRESULT endHr = target_->EndDraw();

    if (SUCCEEDED(endHr)) {
        BLENDFUNCTION blend{};
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = 255;
        blend.AlphaFormat = AC_SRC_ALPHA;
        POINT ptDst{};
        RECT winRect{};
        GetWindowRect(hwnd_, &winRect);
        ptDst.x = winRect.left;
        ptDst.y = winRect.top;
        HWND parent = GetAncestor(hwnd_, GA_PARENT);
        if (parent && parent != GetDesktopWindow()) {
            ScreenToClient(parent, &ptDst); // 子窗口的 ULW 位置相对父客户区
        }
        SIZE size{w, h};
        POINT ptSrc{0, 0};
        UpdateLayeredWindow(hwnd_, hdcScreen, &ptDst, &size, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);
        painted_ = true;
    } else {
        ReleaseD2D(); // 设备丢失：下个 tick 重建
    }

    SelectObject(hdcMem, oldBmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

LRESULT CALLBACK ClockWidget::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<ClockWidget*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<ClockWidget*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_TIMER:
        if (wp == kClockTimerId) {
            self->OnTick();
            return 0;
        }
        break;
    case WM_PAINT:
        self->OnPaint();
        ValidateRect(hwnd, nullptr);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, kClockTimerId);
        self->hwnd_ = nullptr;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace desktopsticker
