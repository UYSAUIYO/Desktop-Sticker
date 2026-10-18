#pragma once

// 桌面壁纸鼠标交互的路由规则（纯逻辑，不碰 OS，可被 dtest 直接单测）。
//
// 背景：壁纸窗口是桌面 WorkerW 的子窗口、位于图标层之下，而且命中测试一律
// HTTRANSPARENT —— 系统永远不会把鼠标点击交给它。所以"壁纸能交互"只能靠
// 低级鼠标钩子：钩子看到事件后判断这个点是不是**空白桌面**，是就转发给后端，
// 并吞掉原事件（否则点击会同时在桌面上拖出选择框 / 弹出右键菜单）。
//
// 本文件只放"该不该转发 / 该不该吞掉 / 谁抢到了这一点"的判断；
// 钩子线程、窗口命中测试、图标命中测试都在 src/MouseRelay.cpp。

#include <cstdint>

namespace desktopsticker::wallpaper {

// 转发给后端的事件种类（与平台无关；Web 与场景后端各自翻译）。
enum class RelayEventKind {
    Move,
    Leave, // 光标离开可交互区域：给页面/场景一个"鼠标移出"的机会
    LeftDown,
    LeftUp,
    RightDown,
    RightUp,
    MiddleDown,
    MiddleUp,
    Wheel,
    HorizontalWheel,
};

// 鼠标按钮位（与上述 kind 对应）
enum RelayButton { kRelayLeft = 1, kRelayRight = 2, kRelayMiddle = 4 };

inline int relay_button_of(RelayEventKind kind) {
    switch (kind) {
        case RelayEventKind::LeftDown:
        case RelayEventKind::LeftUp:
            return kRelayLeft;
        case RelayEventKind::RightDown:
        case RelayEventKind::RightUp:
            return kRelayRight;
        case RelayEventKind::MiddleDown:
        case RelayEventKind::MiddleUp:
            return kRelayMiddle;
        default:
            return 0;
    }
}

inline bool relay_is_down(RelayEventKind kind) {
    return kind == RelayEventKind::LeftDown || kind == RelayEventKind::RightDown ||
           kind == RelayEventKind::MiddleDown;
}

inline bool relay_is_up(RelayEventKind kind) {
    return kind == RelayEventKind::LeftUp || kind == RelayEventKind::RightUp ||
           kind == RelayEventKind::MiddleUp;
}

// 该屏幕点的命中分类（由 OS 层填）。
struct RelayHit {
    bool desktopLayer = false; // 命中的是桌面（Progman / WorkerW / SHELLDLL_DefView / 图标列表）
    bool overIcon = false;     // 落在桌面图标上（图标优先，绝不转发）
    bool ownSurface = false;   // 命中本进程自己的窗口（分区卡片 / 壁纸 / 时钟 / 对话框）
};

// 可交互 = 空白桌面。图标归图标，自家窗口归自家窗口。
inline bool relay_accepts(const RelayHit& hit) {
    return hit.desktopLayer && !hit.overIcon && !hit.ownSurface;
}

// 壁纸可交互区域（屏幕坐标）。桌面壁纸只铺主显示器，所以这就是那块矩形。
struct RelayViewport {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

inline bool relay_viewport_contains(const RelayViewport& vp, int x, int y) {
    return x >= vp.x && y >= vp.y && x < vp.x + vp.width && y < vp.y + vp.height;
}

// 屏幕 → 壁纸窗口客户区。clamp=true 时把越界点压到边缘：按住按钮拖出画面时
// 事件仍然连续（与真实窗口的鼠标捕获语义一致）。
inline bool relay_to_client(const RelayViewport& vp, int x, int y, bool clamp,
                            int& outX, int& outY) {
    if (!clamp && !relay_viewport_contains(vp, x, y)) return false;
    int cx = x - vp.x;
    int cy = y - vp.y;
    if (clamp) {
        if (vp.width > 0) {
            if (cx < 0) cx = 0;
            if (cx > vp.width - 1) cx = vp.width - 1;
        }
        if (vp.height > 0) {
            if (cy < 0) cy = 0;
            if (cy > vp.height - 1) cy = vp.height - 1;
        }
    }
    outX = cx;
    outY = cy;
    return true;
}

// 悬停节流：低轮询率下 1000Hz 的鼠标会把页面淹掉；按住按钮时不节流（拖动要跟手）。
inline int64_t relay_move_interval_ms(bool capturing) {
    return capturing ? 0 : 8;
}

// 系统事件要不要吞掉：按键与滚轮一律吞（否则桌面会同时响应），
// 移动只在"按住按钮"时吞（未按住时让桌面维持自己的悬停/光标状态）。
inline bool relay_swallows(RelayEventKind kind, bool capturing) {
    if (kind == RelayEventKind::Move) return capturing;
    return true;
}

// 交互状态机：谁抢到了这一点、要不要转发、按下是否已被转发。
class RelayPolicy {
public:
    void set_enabled(bool on) {
        enabled_ = on;
        if (!on) reset();
    }
    bool enabled() const { return enabled_; }

    bool capturing() const { return buttons_ != 0; }
    int forwarded_buttons() const { return buttons_; }

    // 要不要把这一条转给后端（不改状态，转发后用 note_forwarded 更新）。
    bool should_forward(const RelayHit& hit, RelayEventKind kind, int64_t nowMs) const {
        if (!enabled_) return false;

        const int bit = relay_button_of(kind);
        if (capturing()) {
            // 拖动中：不再看命中（光标可以移到分区/别的窗口上），只做配对检查
            if (relay_is_down(kind)) return (buttons_ & bit) == 0;
            if (relay_is_up(kind)) return (buttons_ & bit) != 0;
            return true;
        }

        if (!relay_accepts(hit)) return false;

        if (relay_is_up(kind)) return false; // 没转发过按下，抬起也不能单独转（否则页面收到莫名点击）
        if (relay_is_down(kind)) return true;
        if (kind == RelayEventKind::Move) {
            return nowMs - lastMoveMs_ >= relay_move_interval_ms(false);
        }
        return true; // 滚轮
    }

    void note_forwarded(RelayEventKind kind, int64_t nowMs) {
        const int bit = relay_button_of(kind);
        if (relay_is_down(kind)) {
            buttons_ |= bit;
        } else if (relay_is_up(kind)) {
            buttons_ &= ~bit;
        } else if (kind == RelayEventKind::Move) {
            lastMoveMs_ = nowMs;
        }
    }

    void reset() {
        buttons_ = 0;
        lastMoveMs_ = -100000;
    }

private:
    bool enabled_ = true;
    int buttons_ = 0;          // 已转发且尚未抬起的按钮位
    int64_t lastMoveMs_ = -100000;
};

} // namespace desktopsticker::wallpaper
