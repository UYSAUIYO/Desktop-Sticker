#include "pch.h"
#include "desktopsticker/DesktopWorkspace.h"

#include <windowsx.h>
#include <commctrl.h>
#include <cstdlib>
#include <cstring>

#include "desktopsticker/KnownFolders.h"
#include "desktopsticker/LayoutMath.h"
#include "desktopsticker/Log.h"
#include "desktopsticker/RenameDialog.h"

namespace fs = std::filesystem;

namespace desktopsticker {

namespace {

// 图标“移到屏幕外”使用 (-32000,-32000)；小于该值视为无效/被污染的位置记录
constexpr LONG kOffscreenCoord = -30000;
const UINT kWatcherNotifyMsg = WM_APP + 2;
constexpr int kClassVersion = 2; // 分类规则版本（扩充关键词 + 实用工具分区）

} // namespace

DesktopWorkspace::DesktopWorkspace(ConfigStore* config, IconService* icons,
                                   const std::function<void()>& zonesChanged)
    : config_(config), iconService_(icons), zonesChanged_(zonesChanged),
      layoutPath_(config->GetRootDir() / L"layout.json") {}

DesktopWorkspace::~DesktopWorkspace() {
    Shutdown();
}

bool DesktopWorkspace::Initialize() {
    dstklog::Write(L"workspace", L"Initialize begin");
    try {
        oleInitialized_ = SUCCEEDED(OleInitialize(nullptr));

        if (!shell_.Initialize()) {
            dstklog::Write(L"workspace", L"shell_.Initialize() = false (degraded)");
        } else {
            const auto& w = shell_.Windows();
            dstklog::Write(L"workspace", L"shell ok: progman=" + std::to_wstring(reinterpret_cast<uintptr_t>(w.progman)) +
                           L" workerw=" + std::to_wstring(reinterpret_cast<uintptr_t>(w.workerw)) +
                           L" defView=" + std::to_wstring(reinterpret_cast<uintptr_t>(w.defView)) +
                           L" listView=" + std::to_wstring(reinterpret_cast<uintptr_t>(w.listView)) +
                           L" defViewVisible=" + std::to_wstring(IsWindowVisible(w.defView) ? 1 : 0) +
                           L" workerwVisible=" + std::to_wstring(IsWindowVisible(w.workerw) ? 1 : 0));
        }

        iconManager_ = std::make_unique<DesktopIconManager>(&shell_);

        // 记录自动排列状态（但不要跨进程修改 Explorer 窗口样式，会崩 Explorer）
        if (iconManager_->ListView() && IsWindow(iconManager_->ListView())) {
            LONG_PTR style = GetWindowLongPtrW(iconManager_->ListView(), GWL_STYLE);
            model_.Layout().autoArrangeWasEnabled = (style & LVS_AUTOARRANGE) != 0;
            dstklog::Write(L"workspace", L"listview found, autoArrange=" +
                           std::to_wstring(model_.Layout().autoArrangeWasEnabled ? 1 : 0));
        } else {
            dstklog::Write(L"workspace", L"listview NOT found");
        }

        // 已有布局则加载；若没有图标、仍是旧分类名、或分类版本过旧则重新自动分类
        bool hasItems = false;
        bool needsReclassify = false;
        if (model_.Load(layoutPath_)) {
            for (const auto& z : model_.Layout().zones) {
                if (!z.itemPaths.empty()) hasItems = true;
                if (z.name == L"文档" || z.name == L"图片" || z.name == L"视频" || z.name == L"音乐") {
                    needsReclassify = true;
                }
            }
            if (model_.Layout().version < 2) needsReclassify = true;
            if (model_.Layout().classVersion < kClassVersion) needsReclassify = true;
        }
        if (model_.Layout().zones.empty() || !hasItems || needsReclassify) {
            auto icons = iconManager_->EnumIcons();
            dstklog::Write(L"workspace", L"desktop icons count=" + std::to_wstring(icons.size()));
            AutoClassify(icons);
            SaveLayout();
        }
        dstklog::Write(L"workspace", L"zones=" + std::to_wstring(model_.Layout().zones.size()) +
                       L" items=" + std::to_wstring(CountZoneItems()));

        // 布局版本升级：旧布局默认展开全部分区，避免“折叠后看不到图标”造成困惑
        if (model_.Layout().version < 3) {
            for (auto& z : model_.Layout().zones) z.collapsed = false;
            model_.Layout().version = 3;
            SaveLayout();
        }

        // 布局版本 7：四列布局 + 动态列高（对齐任务栏），列容量可配置（旧版一并升级）
        if (model_.Layout().version < 7) {
            ApplyCompactColumnLayout();
            SaveLayout();
            dstklog::Write(L"workspace", L"layout migrated to compact quad columns");
        }

        // 清理历史版本误记录的无效原始位置（-32000 是我们自己移出去的，恢复无意义）
        {
            bool removed = false;
            for (auto it = model_.Layout().originalIconPositions.begin();
                 it != model_.Layout().originalIconPositions.end();) {
                if (it->second.x <= kOffscreenCoord || it->second.y <= kOffscreenCoord) {
                    it = model_.Layout().originalIconPositions.erase(it);
                    removed = true;
                } else {
                    ++it;
                }
            }
            if (removed) SaveLayout();
        }

        // 直接隐藏整个桌面图标列表（比逐图标移出更简单可靠）
        {
            const bool hid = iconManager_->HideAllIcons(true);
            dstklog::Write(L"workspace", L"hide desktop icons=" + std::to_wstring(hid ? 1 : 0));
        }

        CreateMessageWindow();
        CreateZoneWindows();
        // 低级鼠标钩子：降级模式下转发输入给分区，并检测双击桌面空白切换干净桌面
        inputForwarder_ = std::make_unique<MouseInputForwarder>(
            &shell_, [this](POINT pt) { return ZoneAtPoint(pt); });
        inputForwarder_->isCleanMode = [this]() { return cleanMode_; };
        // 手势可由设置关闭：每次触发时读最新配置，设置页改动即时生效
        inputForwarder_->onBlankDesktopDoubleClick = [this]() {
            if (config_->GetConfig().dblClickCleanMode) ToggleCleanDesktop();
        };
        inputForwarder_->Start();
        StartDesktopWatcher();
        if (config_->GetConfig().showClock) CreateClock();
        dstklog::Write(L"workspace", L"Initialize end, zoneWindows=" + std::to_wstring(zoneWindows_.size()));
        return true;
    } catch (const std::exception& e) {
        dstklog::Write(L"workspace", L"Initialize exception: " + std::wstring(e.what(), e.what() + strlen(e.what())));
        return false;
    } catch (...) {
        dstklog::Write(L"workspace", L"Initialize unknown exception");
        return false;
    }
}

void DesktopWorkspace::Shutdown() {
    dstklog::Write(L"workspace", L"Shutdown begin");
    try {
        desktopWatcher_.reset();
        if (inputForwarder_) inputForwarder_->Stop();
        DestroyZoneWindows();
        if (clock_) {
            clock_->Destroy();
            clock_.reset();
        }
        RestoreDesktop();
    } catch (...) {
        dstklog::Write(L"workspace", L"Shutdown cleanup exception");
    }
    if (msgHwnd_) {
        DestroyWindow(msgHwnd_);
        msgHwnd_ = nullptr;
    }
    iconManager_.reset();
    shell_.Shutdown();
    if (oleInitialized_) {
        OleUninitialize();
        oleInitialized_ = false;
    }
    dstklog::Write(L"workspace", L"Shutdown end");
}

void DesktopWorkspace::Refresh() {
    SyncZoneWindows();
    if (zonesChanged_) zonesChanged_();
}

void DesktopWorkspace::SyncZoneWindows() {
    // 把 model 数据同步到窗口（折叠/重命名/缩放立即生效），并销毁已删除分区的窗口
    std::vector<std::unique_ptr<ZoneWindow>> keep;
    keep.reserve(zoneWindows_.size());
    for (auto& w : zoneWindows_) {
        Zone* z = model_.FindZone(w->GetZone().id);
        if (!z) {
            DestroyZoneWindow(w.get());
        } else {
            w->SetZone(*z);
            keep.push_back(std::move(w));
        }
    }
    zoneWindows_ = std::move(keep);
    for (auto& w : zoneWindows_) w->Refresh();
}

void DesktopWorkspace::CreateMessageWindow() {
    HINSTANCE hInst = GetModuleHandleW(L"DesktopSticker.Features.dll");
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WorkspaceMsgProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"DesktopSticker.WorkspaceMsg";
    RegisterClassExW(&wc);
    msgHwnd_ = CreateWindowExW(0, L"DesktopSticker.WorkspaceMsg", L"", 0,
                               0, 0, 0, 0, HWND_MESSAGE, nullptr, hInst, this);
}

LRESULT CALLBACK DesktopWorkspace::WorkspaceMsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<DesktopWorkspace*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<DesktopWorkspace*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    if (msg == kWatcherNotifyMsg && self) {
        self->CollectNewDesktopIcons();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void DesktopWorkspace::SetZoneSpacing(int columnSpacing, int rowSpacing) {
    for (auto& w : zoneWindows_) w->SetSpacing(columnSpacing, rowSpacing);
}

void DesktopWorkspace::RelayoutZones() {
    ApplyCompactColumnLayout();
    SaveLayout();
    Refresh();
}

void DesktopWorkspace::CreateClock() {
    if (clock_) {
        if (clock_->Hwnd()) ShowWindow(clock_->Hwnd(), SW_SHOW);
        return;
    }
    ClockWidget::RegisterClass(GetModuleHandleW(L"DesktopSticker.Features.dll"));
    clock_ = std::make_unique<ClockWidget>(GetModuleHandleW(L"DesktopSticker.Features.dll"));
    if (!clock_->Create()) {
        dstklog::Write(L"workspace", L"clock create FAILED");
        clock_.reset();
        return;
    }
    // 位置：工作区顶部居中（左右四列磁贴，中间顶部留给时钟）
    RECT work{};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        work.left = 0;
        work.right = GetSystemMetrics(SM_CXSCREEN);
        work.top = 0;
    }
    SetWindowPos(clock_->Hwnd(), nullptr,
                 (work.left + work.right - clock_->Width()) / 2, work.top + 24,
                 clock_->Width(), clock_->Height(), SWP_NOACTIVATE | SWP_NOZORDER);
    // 与磁贴同一套嵌入路径：defView 子窗口；失败则退化为最底层弹窗
    if (shell_.IsReady()) {
        const HWND parent = shell_.Windows().defView;
        const HWND oldParent = SetParent(clock_->Hwnd(), parent);
        if (oldParent || GetLastError() == 0) clock_->SetEmbedded(true);
        LONG_PTR style = GetWindowLongPtrW(clock_->Hwnd(), GWL_STYLE);
        SetWindowLongPtrW(clock_->Hwnd(), GWL_STYLE, (style & ~WS_POPUP) | WS_CHILD);
        SetWindowPos(clock_->Hwnd(), HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else {
        SetWindowPos(clock_->Hwnd(), HWND_BOTTOM, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    clock_->Refresh(); // 嵌入后强制重绘：SetParent/样式切换会重置分层表面
    dstklog::Write(L"workspace",
                   L"clock created embedded=" + std::to_wstring(clock_->IsEmbedded() ? 1 : 0));
}

void DesktopWorkspace::SetClockVisible(bool show) {
    if (show) {
        CreateClock(); // 已存在则仅显示
    } else if (clock_ && clock_->Hwnd()) {
        ShowWindow(clock_->Hwnd(), SW_HIDE);
    }
}

size_t DesktopWorkspace::CountZoneItems() const {
    size_t n = 0;
    for (const auto& z : model_.Layout().zones) n += z.itemPaths.size();
    return n;
}

void DesktopWorkspace::ApplyCompactColumnLayout() {
    // 目标排布（用户指定初始布局）：左右各两列贴边；卡片高度按列动态均分工作区高度，
    // 列底对齐任务栏上沿（底部不留空档）；每列卡片数（4/5）由设置页控制。
    // 分布数学在 LayoutMath.h（纯函数，可单测），这里只负责写回 zones。
    RECT work{};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        work.top = 0;
        work.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    QuadColumnParams params{};
    params.screenW = GetSystemMetrics(SM_CXSCREEN);
    params.workTop = work.top;
    params.workBottom = work.bottom;
    params.maxPerCol = std::clamp(config_->GetConfig().zoneColumnCards, 4, 5);
    const auto rects = ComputeQuadColumnRects(static_cast<int>(model_.Layout().zones.size()), params);
    auto& zones = model_.Layout().zones;
    for (size_t i = 0; i < rects.size() && i < zones.size(); ++i) {
        zones[i].rect = rects[i];
        zones[i].monitorIndex = 0;
    }
    model_.Layout().version = 7; // 动态高度四列布局版本
}

void DesktopWorkspace::AutoClassify(const std::vector<DesktopIconInfo>& icons) {
    // 重新分类时清空旧分区（原图标位置记录保留，恢复桌面依赖它）
    model_.Layout().zones.clear();
    dstklog::Write(L"workspace", L"AutoClassify icons=" + std::to_wstring(icons.size()));

    const wchar_t* names[] = {L"应用", L"开发工具", L"办公软件", L"浏览器",
                              L"影音娱乐", L"实用工具", L"社交聊天", L"游戏",
                              L"文件夹", L"其他"};
    model_.Layout().version = 2; // 当前分类规则版本
    for (const wchar_t* name : names) {
        Zone z;
        z.id = model_.GenerateZoneId();
        z.name = name;
        z.monitorIndex = 0;
        model_.AddZone(std::move(z));
    }
    ApplyCompactColumnLayout(); // 紧凑四列布局
    model_.Layout().classVersion = kClassVersion;

    for (const auto& icon : icons) {
        if (icon.path.empty()) continue;
        std::wstring category = classifier_.ClassifyPath(icon.path);
        Zone* zone = model_.FindZoneByName(category);
        if (!zone) continue;
        zone->itemPaths.push_back(icon.path);
        // 只记录移动前的真实位置；重分类时旧图标已在屏幕外，记下 -32000 会导致原位永久丢失
        if (icon.position.x > kOffscreenCoord && icon.position.y > kOffscreenCoord) {
            model_.Layout().originalIconPositions[icon.path] = icon.position;
        }
        iconManager_->MoveIconOffscreen(icon.index);
    }
    dstklog::Write(L"workspace", L"AutoClassify done items=" + std::to_wstring(CountZoneItems()));
}

void DesktopWorkspace::CollectIntoZone(const std::wstring& zoneId, const std::wstring& path) {
    Zone* zone = model_.FindZone(zoneId);
    if (!zone) return;
    for (const auto& existing : zone->itemPaths) {
        if (_wcsicmp(existing.c_str(), path.c_str()) == 0) return;
    }
    zone->itemPaths.push_back(path);
    auto icons = iconManager_->EnumIcons();
    for (const auto& icon : icons) {
        if (_wcsicmp(icon.path.c_str(), path.c_str()) == 0) {
            if (model_.Layout().originalIconPositions.find(path) == model_.Layout().originalIconPositions.end() &&
                icon.position.x > kOffscreenCoord && icon.position.y > kOffscreenCoord) {
                model_.Layout().originalIconPositions[path] = icon.position;
            }
            iconManager_->MoveIconOffscreen(icon.index);
        }
    }
    SaveLayout();
    Refresh();
}

void DesktopWorkspace::RemoveFromZone(const std::wstring& zoneId, const std::wstring& path) {
    Zone* zone = model_.FindZone(zoneId);
    if (!zone) return;
    auto it = std::find(zone->itemPaths.begin(), zone->itemPaths.end(), path);
    if (it == zone->itemPaths.end()) return;
    zone->itemPaths.erase(it);
    auto icons = iconManager_->EnumIcons();
    for (const auto& icon : icons) {
        if (_wcsicmp(icon.path.c_str(), path.c_str()) == 0) {
            POINT target{-32000, -32000};
            auto posIt = model_.Layout().originalIconPositions.find(path);
            if (posIt != model_.Layout().originalIconPositions.end() &&
                posIt->second.x > kOffscreenCoord && posIt->second.y > kOffscreenCoord) {
                target = posIt->second;
            } else {
                // 位置记录缺失/被污染（历史数据）：放到左上角附近的空位，避免图标“消失”
                static int s_fallbackSlot = 0;
                target = POINT{20 + (s_fallbackSlot % 12) * 84, 20 + (s_fallbackSlot / 12) * 100};
                ++s_fallbackSlot;
            }
            iconManager_->RestoreIcon(icon.index, target);
        }
    }
    SaveLayout();
    Refresh();
}

void DesktopWorkspace::CreateZoneWindows() {
    ZoneWindow::RegisterClass(GetModuleHandleW(L"DesktopSticker.Features.dll"));
    const auto& cfg = config_->GetConfig();
    for (const auto& zone : model_.Layout().zones) {
        auto win = std::make_unique<ZoneWindow>(GetModuleHandleW(L"DesktopSticker.Features.dll"),
                                                zone, iconService_,
                                                cfg.zoneColumnSpacing, cfg.zoneRowSpacing);

        win->onCollapseToggle = [this](const std::wstring& zoneId) {
            if (Zone* z = model_.FindZone(zoneId)) {
                z->collapsed = !z->collapsed;
                SaveLayout();
                Refresh();
            }
        };

        win->onRemoveItem = [this](const std::wstring& zoneId, const std::wstring& itemPath) {
            RemoveFromZone(zoneId, itemPath);
        };

        win->onRenameZone = [this](const std::wstring& zoneId) {
            Zone* z = model_.FindZone(zoneId);
            if (!z) return;
            std::wstring newName = z->name;
            HWND owner = zoneWindows_.empty() ? nullptr : zoneWindows_.front()->Hwnd();
            if (ShowRenameDialog(owner, L"重命名分区", newName) && !newName.empty()) {
                z->name = newName;
                SaveLayout();
                Refresh();
            }
        };

        win->onDeleteZone = [this](const std::wstring& zoneId) {
            if (model_.RemoveZone(zoneId)) {
                SaveLayout();
                Refresh();
            }
        };

        win->onTileDrop = [this](const std::wstring& fromZoneId, const std::wstring& itemPath, POINT pt) {
            if (!model_.FindZone(fromZoneId)) return;
            ZoneWindow* targetWindow = ZoneAtPoint(pt); // 落点所在分区窗口（含可见性过滤）
            const std::wstring targetId = targetWindow ? targetWindow->GetZone().id : std::wstring();
            if (!targetId.empty() && targetId != fromZoneId) {
                if (model_.MoveItem(itemPath, fromZoneId, targetId)) {
                    SaveLayout();
                    Refresh();
                }
            } else if (targetId.empty()) {
                // 拖出分区 → 恢复为原生桌面图标
                RemoveFromZone(fromZoneId, itemPath);
            }
        };

        win->onGeometryChanged = [this](const std::wstring& zoneId, const RECT& rect) {
            if (Zone* z = model_.FindZone(zoneId)) {
                z->rect = rect; // 拖动/缩放结果写回 model 并落盘
                SaveLayout();
            }
        };

        if (!win->Create()) {
            dstklog::Write(L"workspace", L"ZoneWindow create FAILED: " + zone.name);
            continue;
        }
        dstklog::Write(L"workspace",
                 L"ZoneWindow created: " + zone.name +
                 L" parent=" + std::to_wstring(reinterpret_cast<uintptr_t>(GetParent(win->Hwnd()))) +
                 L" owner=" + std::to_wstring(reinterpret_cast<uintptr_t>(GetWindow(win->Hwnd(), GW_OWNER))));

        if (shell_.IsReady()) {
            const HWND parent = shell_.Windows().defView;
            const HWND oldParent = SetParent(win->Hwnd(), parent);
            const DWORD err = GetLastError();
            if (oldParent || err == 0) win->SetEmbedded(true);
            // 转成真正的 WS_CHILD 子窗口：被裁剪在桌面范围内，不会遮挡普通窗口
            LONG_PTR style = GetWindowLongPtrW(win->Hwnd(), GWL_STYLE);
            SetWindowLongPtrW(win->Hwnd(), GWL_STYLE, (style & ~WS_POPUP) | WS_CHILD);
            dstklog::Write(L"workspace",
                     L"  after SetParent parent=" +
                     std::to_wstring(reinterpret_cast<uintptr_t>(GetParent(win->Hwnd()))) +
                     L" ancestor=" +
                     std::to_wstring(reinterpret_cast<uintptr_t>(GetAncestor(win->Hwnd(), GA_PARENT))));
            // 嵌入成功放在桌面最上（图标层之上）；失败则退化为最底层置顶弹窗
            if (!oldParent && err == 0) {
                SetWindowPos(win->Hwnd(), HWND_TOP, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            } else {
                SetWindowPos(win->Hwnd(), HWND_BOTTOM, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            RECT rc{};
            GetWindowRect(win->Hwnd(), &rc);
            dstklog::Write(L"workspace",
                     L"ZoneWindow embed: " + zone.name +
                     L" oldParent=" + std::to_wstring(reinterpret_cast<uintptr_t>(oldParent)) +
                     L" err=" + std::to_wstring(err) +
                     L" hwnd=" + std::to_wstring(reinterpret_cast<uintptr_t>(win->Hwnd())) +
                     L" visible=" + std::to_wstring(IsWindowVisible(win->Hwnd()) ? 1 : 0) +
                     L" rect=" + std::to_wstring(rc.left) + L"," + std::to_wstring(rc.top) +
                     L"-" + std::to_wstring(rc.right) + L"," + std::to_wstring(rc.bottom) +
                     L" parent=" + std::to_wstring(reinterpret_cast<uintptr_t>(GetParent(win->Hwnd()))));
        } else {
            SetWindowPos(win->Hwnd(), HWND_BOTTOM, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            dstklog::Write(L"workspace", L"ZoneWindow fallback bottom: " + zone.name);
        }

        // 接收 Shell 拖放（原生图标/文件拖入分区）
        auto drop = new DropTarget(win->Hwnd(), [this, zoneId = zone.id](const std::vector<std::wstring>& paths) {
            for (const auto& path : paths) {
                CollectIntoZone(zoneId, path);
            }
        });
        if (SUCCEEDED(RegisterDragDrop(win->Hwnd(), drop))) {
            dropTargets_.push_back(drop);
        } else {
            drop->Release();
        }

        zoneWindows_.push_back(std::move(win));
        // 分层窗口需要主动触发首帧绘制（UpdateLayeredWindow）
        zoneWindows_.back()->Refresh();
    }
}

void DesktopWorkspace::DestroyZoneWindow(ZoneWindow* window) {
    if (!window) return;
    DetachDropTarget(window->Hwnd());
    window->Destroy();
}

void DesktopWorkspace::DetachDropTarget(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) {
        // 窗口已销毁：仍需清理对应的 DropTarget 引用
        for (auto it = dropTargets_.begin(); it != dropTargets_.end(); ++it) {
            if (*it && !IsWindow((*it)->Hwnd())) {
                (*it)->Release();
                dropTargets_.erase(it);
                return;
            }
        }
        return;
    }
    for (auto it = dropTargets_.begin(); it != dropTargets_.end(); ++it) {
        if (*it && (*it)->Hwnd() == hwnd) {
            RevokeDragDrop(hwnd);
            (*it)->Release();
            dropTargets_.erase(it);
            return;
        }
    }
}

void DesktopWorkspace::DestroyZoneWindows() {
    for (auto& w : zoneWindows_) DetachDropTarget(w->Hwnd());
    zoneWindows_.clear();
    ZoneWindow::UnregisterClass(GetModuleHandleW(L"DesktopSticker.Features.dll"));
}

void DesktopWorkspace::ToggleCleanDesktop() {
    cleanMode_ = !cleanMode_;
    for (auto& w : zoneWindows_) {
        ShowWindow(w->Hwnd(), cleanMode_ ? SW_HIDE : SW_SHOW);
    }
    if (clock_ && clock_->Hwnd()) {
        ShowWindow(clock_->Hwnd(), cleanMode_ ? SW_HIDE : SW_SHOW);
    }
    if (iconManager_) iconManager_->HideAllIcons(cleanMode_);
}

void DesktopWorkspace::RestoreDesktop() {
    if (!iconManager_) return;
    // 干净桌面模式下先恢复分区显示，避免托盘“恢复桌面”后 cleanMode_ 与实际显隐脱节
    // （否则下次双击桌面空白的行为会反一次）
    if (cleanMode_) ToggleCleanDesktop();
    // 退出/还原：把仍在屏幕外的图标按记录的原始位置放回去，再显示列表。
    // 只动“还在屏幕外”的图标，用户手动整理过的位置不受影响；无位置记录的放到左上角空位。
    auto icons = iconManager_->EnumIcons();
    int restored = 0;
    for (const auto& icon : icons) {
        if (icon.path.empty()) continue;
        if (icon.position.x > kOffscreenCoord && icon.position.y > kOffscreenCoord) continue;
        POINT target{-32000, -32000};
        auto posIt = model_.Layout().originalIconPositions.find(icon.path);
        if (posIt != model_.Layout().originalIconPositions.end() &&
            posIt->second.x > kOffscreenCoord && posIt->second.y > kOffscreenCoord) {
            target = posIt->second;
        } else {
            static int s_fallbackSlot = 0;
            target = POINT{20 + (s_fallbackSlot % 12) * 84, 20 + (s_fallbackSlot / 12) * 100};
            ++s_fallbackSlot;
        }
        iconManager_->RestoreIcon(icon.index, target);
        ++restored;
    }
    iconManager_->HideAllIcons(false); // 显示原生桌面图标（启动时隐藏了整个列表）
    dstklog::Write(L"workspace", L"RestoreDesktop restored=" + std::to_wstring(restored));
}

void DesktopWorkspace::SaveLayout() {
    model_.Save(layoutPath_);
}

void DesktopWorkspace::StartDesktopWatcher() {
    const std::wstring desktopPath = GetKnownPath(FOLDERID_Desktop);
    if (desktopPath.empty()) return;

    // watcher 回调在后台线程触发，这里只投递消息，实际收集逻辑封送回 UI 线程执行，
    // 避免后台线程与交互操作并发读写 layout 数据
    desktopWatcher_ = std::make_unique<DirectoryWatcher>(desktopPath, [this]() {
        if (msgHwnd_) PostMessageW(msgHwnd_, kWatcherNotifyMsg, 0, 0);
    });
    desktopWatcher_->Start();
}

void DesktopWorkspace::CollectNewDesktopIcons() {
    if (!iconManager_) return;
    auto icons = iconManager_->EnumIcons();
    for (const auto& icon : icons) {
        if (icon.path.empty()) continue;
        bool alreadyCollected = false;
        for (const auto& zone : model_.Layout().zones) {
            for (const auto& p : zone.itemPaths) {
                if (_wcsicmp(p.c_str(), icon.path.c_str()) == 0) {
                    alreadyCollected = true;
                    break;
                }
            }
            if (alreadyCollected) break;
        }
        if (alreadyCollected) continue;
        std::wstring category = classifier_.ClassifyPath(icon.path);
        Zone* zone = model_.FindZoneByName(category);
        if (zone) {
            zone->itemPaths.push_back(icon.path);
            if (icon.position.x > kOffscreenCoord && icon.position.y > kOffscreenCoord) {
                model_.Layout().originalIconPositions[icon.path] = icon.position;
            }
            iconManager_->MoveIconOffscreen(icon.index);
        }
    }
    SaveLayout();
    Refresh();
}

ZoneWindow* DesktopWorkspace::ZoneAtPoint(POINT pt) const {
    for (const auto& w : zoneWindows_) {
        if (!w || !w->Hwnd()) continue;
        if (!IsWindowVisible(w->Hwnd())) continue; // 干净桌面模式下隐藏的分区不拦截鼠标
        RECT rc{};
        if (GetWindowRect(w->Hwnd(), &rc) && PtInRect(&rc, pt)) return w.get();
    }
    return nullptr;
}

} // namespace desktopsticker
