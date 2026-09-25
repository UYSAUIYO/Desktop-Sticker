#pragma once

// 呈现让位状态机（纯逻辑，不碰 DComp）。
//
// 背景：D3dContext（D3D11 + DComp 交换链）只服务"产帧型"后端。
// 切到"自呈现型"后端（Web / Vulkan）时必须让 DComp visual 让位，否则会盖住它们；
// 这一段写错只会表现为"黑屏"或"被盖住"，肉眼才发现，所以做成显式状态机并单测。

namespace desktopsticker::wallpaper {

enum class Presentation { FrameProducer, SelfPresenting };

class PresentationArbiter {
public:
    struct Decision {
        bool change = false;              // 是否需要改变呈现方式
        Presentation target = Presentation::FrameProducer;
    };

    // 后端 Open 结束时调用。openOk=false 表示该后端没能起来：
    // 此时**不改动**当前呈现方式 —— 失败的尝试不该把正在工作的画面搞黑。
    Decision OnBackendOpen(bool selfPresenting, bool openOk) {
        if (!openOk) return {false, current_};
        const Presentation want = selfPresenting ? Presentation::SelfPresenting
                                                : Presentation::FrameProducer;
        if (want == current_) return {false, current_};
        current_ = want;
        return {true, want};
    }

    // 后端关闭：回到产帧型（D3dContext 负责显示空闲底色）。
    Decision OnBackendClosed() {
        if (current_ == Presentation::FrameProducer) return {false, current_};
        current_ = Presentation::FrameProducer;
        return {true, Presentation::FrameProducer};
    }

    Presentation Current() const { return current_; }

private:
    Presentation current_ = Presentation::FrameProducer;
};

} // namespace desktopsticker::wallpaper
