#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/wallpaper/MouseRelay.h>

using namespace desktopsticker::wallpaper;

// 壁纸鼠标交互的路由规则：谁抢到这一点、该不该转发、该不该吞掉、按下有没有配对。

namespace {

RelayHit blank() {
    RelayHit hit;
    hit.desktopLayer = true;
    return hit;
}

RelayHit icon() {
    RelayHit hit = blank();
    hit.overIcon = true;
    return hit;
}

RelayHit own() {
    RelayHit hit;
    hit.desktopLayer = true;   // 分区卡片也是 WorkerW 的子窗口，只看父链会误判
    hit.ownSurface = true;
    return hit;
}

RelayHit otherApp() {
    return RelayHit{};         // 别的应用的窗口：既不是桌面，也不是我们
}

} // namespace

TEST(MouseRelay_OnlyBlankDesktopIsInteractive) {
    ASSERT_TRUE(relay_accepts(blank()));
    ASSERT_FALSE(relay_accepts(icon()));      // 图标优先
    ASSERT_FALSE(relay_accepts(own()));       // 分区/时钟/壁纸自己
    ASSERT_FALSE(relay_accepts(otherApp()));  // 全屏应用、任务栏、开始菜单
}

TEST(MouseRelay_ScreenToClientAndClamp) {
    RelayViewport vp;
    vp.width = 1920;
    vp.height = 1080;

    int x = -1, y = -1;
    ASSERT_TRUE(relay_to_client(vp, 100, 200, false, x, y));
    ASSERT_EQ(100, x);
    ASSERT_EQ(200, y);

    // 窗口不在屏幕原点时按原点平移
    vp.x = 1920;
    ASSERT_TRUE(relay_to_client(vp, 2000, 50, false, x, y));
    ASSERT_EQ(80, x);
    ASSERT_EQ(50, y);

    // 不裁剪时越界直接拒绝
    ASSERT_FALSE(relay_to_client(vp, 100, 200, false, x, y));

    // 拖动中越界要压到边缘（事件连续，与鼠标捕获一致）
    ASSERT_TRUE(relay_to_client(vp, 100, 200, true, x, y));
    ASSERT_EQ(0, x);
    ASSERT_EQ(200, y);
    vp.x = 0;
    ASSERT_TRUE(relay_to_client(vp, 5000, 5000, true, x, y));
    ASSERT_EQ(1919, x);
    ASSERT_EQ(1079, y);
}

TEST(MouseRelay_ForwardsOnlyInsideDesktop) {
    RelayPolicy policy;

    ASSERT_TRUE(policy.should_forward(blank(), RelayEventKind::LeftDown, 1000));
    ASSERT_FALSE(policy.should_forward(icon(), RelayEventKind::LeftDown, 1000));
    ASSERT_FALSE(policy.should_forward(own(), RelayEventKind::LeftDown, 1000));
    ASSERT_FALSE(policy.should_forward(otherApp(), RelayEventKind::LeftDown, 1000));
    ASSERT_TRUE(policy.enabled());
    ASSERT_FALSE(policy.capturing());
}

TEST(MouseRelay_CaptureKeepsRelayingWhileDraggingOut) {
    RelayPolicy policy;
    ASSERT_TRUE(policy.should_forward(blank(), RelayEventKind::LeftDown, 1000));
    policy.note_forwarded(RelayEventKind::LeftDown, 1000);
    ASSERT_TRUE(policy.capturing());

    // 拖到分区卡片 / 别的应用上方：仍然属于同一次拖动，继续转发
    ASSERT_TRUE(policy.should_forward(own(), RelayEventKind::Move, 1001));
    ASSERT_TRUE(policy.should_forward(otherApp(), RelayEventKind::LeftUp, 1002));
    policy.note_forwarded(RelayEventKind::LeftUp, 1002);
    ASSERT_FALSE(policy.capturing());

    // 松开之后又回到"只认空白桌面"
    ASSERT_FALSE(policy.should_forward(own(), RelayEventKind::Move, 1003));
}

TEST(MouseRelay_UnmatchedUpIsNotForwarded) {
    // 在分区上按下、拖到壁纸上松开：页面不能收到一个没有按下的点击
    RelayPolicy policy;
    ASSERT_FALSE(policy.should_forward(own(), RelayEventKind::LeftDown, 1000));
    ASSERT_FALSE(policy.should_forward(blank(), RelayEventKind::LeftUp, 1100));
}

TEST(MouseRelay_DoesNotForwardDuplicatedDowns) {
    RelayPolicy policy;
    policy.note_forwarded(RelayEventKind::LeftDown, 1000);
    ASSERT_FALSE(policy.should_forward(blank(), RelayEventKind::LeftDown, 1010));
    // 但另一个按钮可以同时按下
    ASSERT_TRUE(policy.should_forward(blank(), RelayEventKind::RightDown, 1011));
    policy.note_forwarded(RelayEventKind::RightDown, 1011);
    ASSERT_EQ(kRelayLeft | kRelayRight, policy.forwarded_buttons());
}

TEST(MouseRelay_MoveIsThrottledButDragIsNot) {
    RelayPolicy policy;
    ASSERT_TRUE(policy.should_forward(blank(), RelayEventKind::Move, 1000));
    policy.note_forwarded(RelayEventKind::Move, 1000);
    // 同一毫秒内的第二条移动：丢掉（1000Hz 鼠标不该把页面淹掉）
    ASSERT_FALSE(policy.should_forward(blank(), RelayEventKind::Move, 1001));
    ASSERT_TRUE(policy.should_forward(blank(), RelayEventKind::Move, 1000 + relay_move_interval_ms(false)));

    // 按住按钮时不再节流
    policy.note_forwarded(RelayEventKind::LeftDown, 2000);
    ASSERT_TRUE(policy.should_forward(blank(), RelayEventKind::Move, 2000));
    ASSERT_TRUE(policy.should_forward(blank(), RelayEventKind::Move, 2000));
}

TEST(MouseRelay_MoveDueGate) {
    // 钩子先问"够久了吗"，不够久连命中测试都不做（WindowFromPoint 很贵）
    RelayPolicy policy;
    ASSERT_TRUE(policy.move_due(1000));
    policy.note_forwarded(RelayEventKind::Move, 1000);
    ASSERT_FALSE(policy.move_due(1001));
    ASSERT_TRUE(policy.move_due(1000 + relay_move_interval_ms(false)));

    // 按住按钮时不再节流
    policy.note_forwarded(RelayEventKind::LeftDown, 2000);
    ASSERT_TRUE(policy.move_due(2000));
    ASSERT_TRUE(policy.move_due(2001));
}

TEST(MouseRelay_WheelIsForwardedOnBlankDesktop) {
    RelayPolicy policy;
    ASSERT_TRUE(policy.should_forward(blank(), RelayEventKind::Wheel, 1000));
    ASSERT_TRUE(policy.should_forward(blank(), RelayEventKind::HorizontalWheel, 1000));
    ASSERT_FALSE(policy.should_forward(icon(), RelayEventKind::Wheel, 1000));
}

TEST(MouseRelay_DisabledForwardsNothing) {
    RelayPolicy policy;
    policy.set_enabled(false);
    ASSERT_FALSE(policy.should_forward(blank(), RelayEventKind::LeftDown, 1000));
    ASSERT_FALSE(policy.should_forward(blank(), RelayEventKind::Move, 1000));

    policy.set_enabled(true);
    ASSERT_TRUE(policy.should_forward(blank(), RelayEventKind::LeftDown, 1000));
}

TEST(MouseRelay_DisableDropsHeldButtons) {
    // 交互中途关掉设置（或暂停）：不能把"按住"的状态带到下一次
    RelayPolicy policy;
    policy.note_forwarded(RelayEventKind::LeftDown, 1000);
    ASSERT_TRUE(policy.capturing());
    policy.set_enabled(false);
    ASSERT_FALSE(policy.capturing());
    ASSERT_EQ(0, policy.forwarded_buttons());
}

TEST(MouseRelay_SwallowRules) {
    // 按键/滚轮一律吞（否则桌面同时拖选择框、弹右键菜单）；移动只在拖动时吞
    ASSERT_TRUE(relay_swallows(RelayEventKind::LeftDown, false));
    ASSERT_TRUE(relay_swallows(RelayEventKind::LeftUp, true));
    ASSERT_TRUE(relay_swallows(RelayEventKind::Wheel, false));
    ASSERT_FALSE(relay_swallows(RelayEventKind::Move, false));
    ASSERT_TRUE(relay_swallows(RelayEventKind::Move, true));
}

TEST(MouseRelay_ReportsOnlyButtonsItActuallyDelivered) {
    // 在图标 / 分区卡片上按下：后端没收到 down，之后的移动绝不能带"按着左键"
    // （Godot 会把它当成一次没有按下的拖动，表现就是抽搐）
    ASSERT_EQ(0, relay_reported_buttons(kRelayLeft, 0, RelayEventKind::Move));

    // 转发过的按下：移动要一直带着，后端才能连贯地拖动
    ASSERT_EQ(kRelayLeft, relay_reported_buttons(kRelayLeft, kRelayLeft, RelayEventKind::Move));

    // 按下事件本身要带上这个键（note_forwarded 在这之后才更新状态）
    ASSERT_EQ(kRelayLeft, relay_reported_buttons(kRelayLeft, 0, RelayEventKind::LeftDown));

    // 抬起事件里不再带它
    ASSERT_EQ(0, relay_reported_buttons(kRelayLeft, kRelayLeft, RelayEventKind::LeftUp));

    // 右键没转发过、左键转发过：只报左键
    ASSERT_EQ(kRelayLeft, relay_reported_buttons(kRelayLeft | kRelayRight, kRelayLeft,
                                                 RelayEventKind::Move));
}

TEST(MouseRelay_ButtonBitsMatchKinds) {
    ASSERT_EQ(kRelayLeft, relay_button_of(RelayEventKind::LeftDown));
    ASSERT_EQ(kRelayLeft, relay_button_of(RelayEventKind::LeftUp));
    ASSERT_EQ(kRelayRight, relay_button_of(RelayEventKind::RightUp));
    ASSERT_EQ(kRelayMiddle, relay_button_of(RelayEventKind::MiddleDown));
    ASSERT_EQ(0, relay_button_of(RelayEventKind::Move));
    ASSERT_TRUE(relay_is_down(RelayEventKind::MiddleDown));
    ASSERT_TRUE(relay_is_up(RelayEventKind::RightUp));
    ASSERT_FALSE(relay_is_down(RelayEventKind::Move));
}

TEST(MouseRelay_DropsMoveSupersededByButton) {
    // 引擎的消息泵每帧先派发按键/滚轮、最后才派发一条移动：同一批里"按钮之前的移动"
    // 会晚于按钮执行，位置比按钮更旧（按下/抬起那一帧画面往回跳 = 抽搐），必须丢掉。
    const RelayEventKind move_then_down[] = {RelayEventKind::Move, RelayEventKind::LeftDown};
    ASSERT_TRUE(relay_move_superseded(move_then_down, 2, 0));
    ASSERT_FALSE(relay_move_superseded(move_then_down, 2, 1));

    // 按钮之后的移动是最新的，要照常转发（拖动靠它跟手）
    const RelayEventKind down_then_move[] = {RelayEventKind::LeftDown, RelayEventKind::Move};
    ASSERT_FALSE(relay_move_superseded(down_then_move, 2, 1));

    // 一批里只有移动：不会被谁顶掉
    const RelayEventKind moves[] = {RelayEventKind::Move, RelayEventKind::Move};
    ASSERT_FALSE(relay_move_superseded(moves, 2, 0));
    ASSERT_FALSE(relay_move_superseded(moves, 2, 1));

    // 抬起前的移动同样丢：松手后不应再冒出一条更旧位置的移动
    const RelayEventKind move_then_up[] = {RelayEventKind::Move, RelayEventKind::LeftUp};
    ASSERT_TRUE(relay_move_superseded(move_then_up, 2, 0));

    // 滚轮也属于"离散消息"
    const RelayEventKind move_then_wheel[] = {RelayEventKind::Move, RelayEventKind::Wheel};
    ASSERT_TRUE(relay_move_superseded(move_then_wheel, 2, 0));

    // 空批 / 越界不炸
    ASSERT_FALSE(relay_move_superseded(nullptr, 0, 0));
    ASSERT_FALSE(relay_move_superseded(moves, 2, 5));
}
