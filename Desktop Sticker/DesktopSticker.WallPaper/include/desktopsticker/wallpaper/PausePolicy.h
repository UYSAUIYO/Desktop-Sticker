#pragma once

// 暂停优先级策略：纯函数，不触碰 OS。
// 全屏检测、会话/显示器状态探测在 FullscreenDetector.cpp。

namespace desktopsticker::wallpaper {

enum class PauseReason { None, SessionLocked, DisplayOff, UserPaused, FullscreenCovered };
enum class PlaybackState { Playing, Paused };

struct PauseInputs {
    bool sessionLocked = false;
    bool displayOff = false;
    bool userPaused = false;
    bool fullscreenCovered = false;
};

struct PauseDecision {
    PlaybackState state = PlaybackState::Playing;
    PauseReason reason = PauseReason::None;
};

// 固定优先级：会话锁定 > 显示器关闭 > 用户手动暂停 > 全屏遮挡 > 正常播放。
inline PauseDecision reduce_pause_policy(const PauseInputs& in) {
    if (in.sessionLocked)     return {PlaybackState::Paused, PauseReason::SessionLocked};
    if (in.displayOff)        return {PlaybackState::Paused, PauseReason::DisplayOff};
    if (in.userPaused)        return {PlaybackState::Paused, PauseReason::UserPaused};
    if (in.fullscreenCovered) return {PlaybackState::Paused, PauseReason::FullscreenCovered};
    return {PlaybackState::Playing, PauseReason::None};
}

} // namespace desktopsticker::wallpaper
