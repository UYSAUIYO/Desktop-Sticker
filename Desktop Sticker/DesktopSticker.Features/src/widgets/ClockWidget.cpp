#include "pch.h"
#include "ClockWidget.h"

#include <cwchar>
#include <dwrite.h>

namespace desktopsticker {

namespace {
const wchar_t kClockWindowClass[] = L"DesktopSticker.ClockWindow";
constexpr UINT_PTR kClockTimerId = 1;
} // namespace

std::wstring ClockText::TimeText(const SYSTEMTIME& st) {
    wchar_t buf[16]{};
    swprintf_s(buf, L"%02d:%02d", st.wHour, st.wMinute);
    return buf;
}

std::wstring ClockText::DateText(const SYSTEMTIME& st) {
    static const wchar_t* kWeekday[] = {L"周日", L"周一", L"周二", L"周三", L"周四", L"周五", L"周六"};
    wchar_t buf[64]{};
    swprintf_s(buf, L"%d年%d月%d日 %s", st.wYear, st.wMonth, st.wDay, kWeekday[st.wDayOfWeek % 7]);
    return buf;
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
    // WS_EX_TRANSPARENT：整窗点击穿透；ULW 逐像素 alpha 决定外观（同磁贴，禁用 SLWA）
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                            kClockWindowClass, L"",
                            WS_POPUP | WS_VISIBLE,
                            100, 100, Width(), Height(),
                            nullptr, nullptr, hInst_, this);
    if (!hwnd_) return false;
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetTimer(hwnd_, kClockTimerId, 1000, nullptr);
    OnTick(); // 首帧立即绘制
    return true;
}

void ClockWidget::Destroy() {
    if (hwnd_ && IsWindow(hwnd_)) {
        KillTimer(hwnd_, kClockTimerId);
        DestroyWindow(hwnd_);
    }
    hwnd_ = nullptr;
    if (paintBmp_) {
        DeleteObject(paintBmp_);
        paintBmp_ = nullptr;
        paintBits_ = nullptr;
    }
    paintW_ = paintH_ = 0;
}

void ClockWidget::OnTick() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    const std::wstring t = ClockText::TimeText(st);
    const std::wstring d = ClockText::DateText(st);
    if (painted_ && t == timeText_ && d == dateText_) return; // 分钟未变不重绘
    timeText_ = t;
    dateText_ = d;
    OnPaint();
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
    if (!bgBrush_) target_->CreateSolidColorBrush(D2D1::ColorF(0x2B2B2B, 0.72f), &bgBrush_);
    if (!borderBrush_) target_->CreateSolidColorBrush(D2D1::ColorF(0xFFFFFF, 0.12f), &borderBrush_);
    if (!timeBrush_) target_->CreateSolidColorBrush(D2D1::ColorF(0xFFFFFF, 0.96f), &timeBrush_);
    if (!dateBrush_) target_->CreateSolidColorBrush(D2D1::ColorF(0xFFFFFF, 0.60f), &dateBrush_);
    if (!timeFormat_) {
        dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                         DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                         42.0f, L"zh-cn", &timeFormat_);
    }
    if (!dateFormat_) {
        dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                         DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                         15.0f, L"zh-cn", &dateFormat_);
    }
    return bgBrush_ && borderBrush_ && timeBrush_ && dateBrush_ && timeFormat_ && dateFormat_;
}

void ClockWidget::ReleaseD2D() {
    if (timeFormat_) timeFormat_->Release();
    if (dateFormat_) dateFormat_->Release();
    if (dateBrush_) dateBrush_->Release();
    if (timeBrush_) timeBrush_->Release();
    if (borderBrush_) borderBrush_->Release();
    if (bgBrush_) bgBrush_->Release();
    if (target_) target_->Release();
    if (dwriteFactory_) dwriteFactory_->Release();
    if (factory_) factory_->Release();
    timeFormat_ = nullptr; dateFormat_ = nullptr;
    dateBrush_ = nullptr; timeBrush_ = nullptr; borderBrush_ = nullptr; bgBrush_ = nullptr;
    target_ = nullptr; dwriteFactory_ = nullptr; factory_ = nullptr;
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
        if (paintBmp_) {
            DeleteObject(paintBmp_);
            paintBmp_ = nullptr;
        }
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
    target_->Clear(D2D1::ColorF(0, 0)); // 圆角外透空

    const float fw = static_cast<float>(w);
    const float fh = static_cast<float>(h);
    target_->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(1, 1, fw - 1, fh - 1), 8.0f, 8.0f), bgBrush_);
    target_->DrawRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, fw - 0.5f, fh - 0.5f), 8.0f, 8.0f),
        borderBrush_, 1.0f);

    target_->DrawTextW(timeText_.c_str(), static_cast<UINT32>(timeText_.size()), timeFormat_,
                       D2D1::RectF(0, 8, fw, 62), timeBrush_);
    target_->DrawTextW(dateText_.c_str(), static_cast<UINT32>(dateText_.size()), dateFormat_,
                       D2D1::RectF(0, 58, fw, fh - 8), dateBrush_);

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
        // 设备丢失：释放资源，下个 tick 重建，否则永久黑屏
        ReleaseD2D();
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
