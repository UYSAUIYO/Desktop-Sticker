#pragma once
#include <functional>

#include "desktopsticker/DesktopShellIntegration.h"
#include "desktopsticker/Export.h"
#include "desktopsticker/ZoneWindow.h"

namespace desktopsticker {

// 低级鼠标钩子（WH_MOUSE_LL），职责有二：
// 1) 降级模式（分区未嵌入桌面、处在最底层）下把按钮事件转发给分区窗口，并手动合成双击；
//    嵌入模式下系统直接命中分区子窗口，绝不转发（否则点击被处理两次）。
// 2) 检测"双击桌面空白"用于切换干净桌面模式（含跨进程 LVM_HITTEST 确认不是点在图标上）。
// 生命周期：Start/Stop 必须与创建分区窗口的线程一致（钩子回调依赖该线程泵消息）。
class DESKTOPSTICKER_API MouseInputForwarder {
public:
    // 给定屏幕坐标返回命中的可见分区窗口（无则 nullptr）
    using ZoneResolver = std::function<ZoneWindow*(POINT)>;

    MouseInputForwarder(DesktopShellIntegration* shell, ZoneResolver resolver);
    ~MouseInputForwarder();

    void Start();
    void Stop();

    // 钩子回调在安装线程执行，宿主必须保证回调内无阻塞（超过 LowLevelHooksTimeout 会被摘钩）
    std::function<bool()> isCleanMode;             // 查询是否处于干净桌面模式
    std::function<void()> onBlankDesktopDoubleClick; // 确认双击桌面空白后回调

private:
    static LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam);
    LRESULT ForwardMouseToZone(ZoneWindow* zone, UINT msg, const POINT& pt);
    void DetectDesktopBlankDoubleClick(const POINT& pt);

    DesktopShellIntegration* shell_;
    ZoneResolver resolveZone_;
    HHOOK hook_ = nullptr;
    // 转发/空白双击判定状态（hook 回调与窗口操作同在 UI 线程，无并发）
    ZoneWindow* forwardDownZone_ = nullptr;
    DWORD forwardDownTick_ = 0;
    POINT forwardDownPt_{};
    DWORD blankDownTick_ = 0;
    POINT blankDownPt_{};
};

} // namespace desktopsticker
