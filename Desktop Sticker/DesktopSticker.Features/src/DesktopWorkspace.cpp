#include "pch.h"
#include "desktopsticker/DesktopWorkspace.h"

#include <windowsx.h>
#include <commctrl.h>
#include <cstdlib>
#include <cstring>

#include "desktopsticker/Utf8.h"

namespace fs = std::filesystem;

namespace desktopsticker {

namespace {

// 图标“移到屏幕外”使用 (-32000,-32000)；小于该值视为无效/被污染的位置记录
constexpr LONG kOffscreenCoord = -30000;
const UINT kWatcherNotifyMsg = WM_APP + 2;

DesktopWorkspace* g_mouseHookWorkspace = nullptr;

void DebugLog(const std::filesystem::path& root, const std::wstring& msg) {
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    std::ofstream out(root / L"debug.log", std::ios::app);
    out << ToUtf8(msg) << std::endl;
}

struct PromptState {
    std::wstring value;
    bool done = false;
    int result = 0;
};

LRESULT CALLBACK PromptWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* state = reinterpret_cast<PromptState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        state = static_cast<PromptState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        CreateWindowExW(0, L"EDIT", state->value.c_str(),
                        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                        10, 10, 210, 24, hwnd, reinterpret_cast<HMENU>(1001), cs->hInstance, nullptr);
        CreateWindowExW(0, L"BUTTON", L"确定",
                        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                        30, 44, 80, 26, hwnd, reinterpret_cast<HMENU>(IDOK), cs->hInstance, nullptr);
        CreateWindowExW(0, L"BUTTON", L"取消",
                        WS_CHILD | WS_VISIBLE,
                        120, 44, 80, 26, hwnd, reinterpret_cast<HMENU>(IDCANCEL), cs->hInstance, nullptr);
        return 0;
    }
    case WM_COMMAND: {
        const int cmd = LOWORD(wp);
        if (cmd == IDOK || cmd == IDCANCEL) {
            if (cmd == IDOK) {
                wchar_t buf[256]{};
                GetDlgItemTextW(hwnd, 1001, buf, 256);
                state->value = buf;
                state->result = IDOK;
            } else {
                state->result = IDCANCEL;
            }
            state->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        state->result = IDCANCEL;
        state->done = true;
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool PromptText(HWND owner, const std::wstring& title, std::wstring& value) {
    static bool registered = false;
    HINSTANCE hInst = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = PromptWndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"DesktopSticker.PromptWindow";
        RegisterClassExW(&wc);
        registered = true;
    }

    PromptState state{value, false, 0};
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"DesktopSticker.PromptWindow", title.c_str(),
                                WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 250, 110,
                                owner, nullptr, hInst, &state);
    if (!hwnd) return false;

    if (owner) EnableWindow(owner, FALSE);
    MSG msg;
    while (!state.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (owner) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    value = state.value;
    return state.result == IDOK;
}

} // namespace

DesktopWorkspace::DesktopWorkspace(ConfigStore* config, const std::function<void()>& zonesChanged)
    : config_(config), zonesChanged_(zonesChanged),
      layoutPath_(config->GetRootDir() / L"layout.json") {}

DesktopWorkspace::~DesktopWorkspace() {
    Shutdown();
}

bool DesktopWorkspace::Initialize() {
    const auto root = config_->GetRootDir();
    DebugLog(root, L"Initialize begin");
    try {
        oleInitialized_ = SUCCEEDED(OleInitialize(nullptr));

        if (!shell_.Initialize()) {
            DebugLog(root, L"shell_.Initialize() = false (degraded)");
        } else {
            const auto& w = shell_.Windows();
            DebugLog(root, L"shell ok: progman=" + std::to_wstring(reinterpret_cast<uintptr_t>(w.progman)) +
                           L" workerw=" + std::to_wstring(reinterpret_cast<uintptr_t>(w.workerw)) +
                           L" defView=" + std::to_wstring(reinterpret_cast<uintptr_t>(w.defView)) +
                           L" listView=" + std::to_wstring(reinterpret_cast<uintptr_t>(w.listView)) +
                           L" defViewVisible=" + std::to_wstring(IsWindowVisible(w.defView) ? 1 : 0) +
                           L" workerwVisible=" + std::to_wstring(IsWindowVisible(w.workerw) ? 1 : 0));
        }

        iconManager_ = std::make_unique<DesktopIconManager>(&shell_);
        iconService_ = std::make_unique<IconService>();

        // 记录自动排列状态（但不要跨进程修改 Explorer 窗口样式，会崩 Explorer）
        if (iconManager_->ListView() && IsWindow(iconManager_->ListView())) {
            LONG_PTR style = GetWindowLongPtrW(iconManager_->ListView(), GWL_STYLE);
            model_.Layout().autoArrangeWasEnabled = (style & LVS_AUTOARRANGE) != 0;
            DebugLog(root, L"listview found, autoArrange=" +
                           std::to_wstring(model_.Layout().autoArrangeWasEnabled ? 1 : 0));
        } else {
            DebugLog(root, L"listview NOT found");
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
        }
        if (model_.Layout().zones.empty() || !hasItems || needsReclassify) {
            auto icons = iconManager_->EnumIcons();
            DebugLog(root, L"desktop icons count=" + std::to_wstring(icons.size()));
            AutoClassify(icons);
            SaveLayout();
        }
        DebugLog(root, L"zones=" + std::to_wstring(model_.Layout().zones.size()) +
                       L" items=" + std::to_wstring(CountZoneItems()));

        // 布局版本升级：旧布局默认展开全部分区，避免“折叠后看不到图标”造成困惑
        if (model_.Layout().version < 3) {
            for (auto& z : model_.Layout().zones) z.collapsed = false;
            model_.Layout().version = 3;
            SaveLayout();
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
            DebugLog(root, L"hide desktop icons=" + std::to_wstring(hid ? 1 : 0));
        }

        CreateMessageWindow();
        CreateZoneWindows();
        StartMouseHook();
        StartDesktopWatcher();
        DebugLog(root, L"Initialize end, zoneWindows=" + std::to_wstring(zoneWindows_.size()));
        return true;
    } catch (const std::exception& e) {
        DebugLog(root, L"Initialize exception: " + std::wstring(e.what(), e.what() + strlen(e.what())));
        return false;
    } catch (...) {
        DebugLog(root, L"Initialize unknown exception");
        return false;
    }
}

void DesktopWorkspace::Shutdown() {
    const auto root = config_->GetRootDir();
    DebugLog(root, L"Shutdown begin");
    try {
        desktopWatcher_.reset();
        StopMouseHook();
        DestroyZoneWindows();
        RestoreDesktop();
    } catch (...) {
        DebugLog(root, L"Shutdown cleanup exception");
    }
    if (msgHwnd_) {
        DestroyWindow(msgHwnd_);
        msgHwnd_ = nullptr;
    }
    iconService_->ClearCache();
    iconManager_.reset();
    shell_.Shutdown();
    if (oleInitialized_) {
        OleUninitialize();
        oleInitialized_ = false;
    }
    DebugLog(root, L"Shutdown end");
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

namespace {

bool ContainsAnyLower(const std::wstring& lower, std::initializer_list<const wchar_t*> keys) {
    for (const wchar_t* key : keys) {
        if (lower.find(key) != std::wstring::npos) return true;
    }
    return false;
}

std::wstring ToLowerCopy(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s;
}

} // namespace

std::wstring DesktopWorkspace::ClassifyPath(const std::wstring& path) {
    fs::path p(path);
    std::wstring name = ToLowerCopy(p.stem().wstring());
    std::wstring fileName = ToLowerCopy(p.filename().wstring());
    std::wstring ext = ToLowerCopy(p.extension().wstring());

    // 文件夹单独归为一类
    if (fs::is_directory(p)) return L"文件夹";

    const bool isApp = ext == L".lnk" || ext == L".exe" || ext == L".appref-ms" || ext == L".url";
    if (isApp) {
        if (ContainsAnyLower(name, {L"visual studio", L"code", L"stm32", L"keil", L"git", L"python",
                                    L"node", L"ide", L"idea", L"arduino", L"kicad", L"cube",
                                    L"docker", L"terminal", L"qt", L"cmake", L"开发", L"编程",
                                    L"compiler", L"ida", L"multisim", L"fusion"}))
            return L"开发工具";
        if (ContainsAnyLower(name, {L"chrome", L"edge", L"firefox", L"浏览器", L"brave", L"360安全浏览器",
                                    L"qq浏览器", L"internet explorer"}))
            return L"浏览器";
        if (ContainsAnyLower(name, {L"word", L"excel", L"powerpoint", L"office", L"wps", L"pdf",
                                    L"onenote", L"outlook", L"办公", L"officeai", L"wps office",
                                    L"xls", L"doc"}))
            return L"办公软件";
        if (ContainsAnyLower(name, {L"potplayer", L"vlc", L"music", L"video", L"播放", L"音乐",
                                    L"网易云", L"qq音乐", L"spotify", L"bilibili", L"爱奇艺",
                                    L"优酷", L"电影", L"影音", L"video lan"}))
            return L"影音娱乐";
        if (ContainsAnyLower(name, {L"微信", L"wechat", L"qq", L"discord", L"telegram", L"钉钉",
                                    L"企业微信", L"slack", L"社交", L"聊天"}))
            return L"社交聊天";
        if (ContainsAnyLower(name, {L"steam", L"epic games", L"wegame", L"origin", L"battle.net",
                                    L"uplay", L"riot", L"valorant", L"gta", L"grand theft auto",
                                    L"minecraft", L"守望先锋", L"绝地求生", L"csgo", L"counter-strike",
                                    L"dota", L"apex", L"fortnite", L"原神", L"genshin", L"崩坏",
                                    L"honkai", L"星穹铁道", L"王者荣耀", L"和平精英", L"英雄联盟",
                                    L"league of legends", L"lol", L"炉石", L"魔兽", L"暗黑",
                                    L"暴雪", L"战网", L"育碧", L"playstation", L"xbox", L"game",
                                    L"games", L"游戏", L"模拟器", L"emulator"}))
            return L"游戏";
        return L"应用";
    }

    // 非应用文件
    if (ext == L".jpg" || ext == L".jpeg" || ext == L".png" || ext == L".gif" || ext == L".bmp" || ext == L".webp")
        return L"其他";
    if (ext == L".mp4" || ext == L".avi" || ext == L".mkv" || ext == L".mov" || ext == L".wmv")
        return L"其他";
    if (ext == L".mp3" || ext == L".wav" || ext == L".flac" || ext == L".aac")
        return L"其他";
    return L"其他";
}

size_t DesktopWorkspace::CountZoneItems() const {
    size_t n = 0;
    for (const auto& z : model_.Layout().zones) n += z.itemPaths.size();
    return n;
}

void DesktopWorkspace::AutoClassify(const std::vector<DesktopIconInfo>& icons) {
    // 重新分类时先清空旧分区，避免重复
    model_.Layout().zones.clear();
    model_.Layout().originalIconPositions.clear();
    DebugLog(config_->GetRootDir(), L"AutoClassify icons=" + std::to_wstring(icons.size()));

    const wchar_t* names[] = {L"应用", L"开发工具", L"办公软件", L"浏览器",
                              L"影音娱乐", L"社交聊天", L"游戏", L"文件夹", L"其他"};
    model_.Layout().version = 2; // 当前分类规则版本
    int monitorIndex = 0;
    int x = 40;
    int y = 60;
    for (const wchar_t* name : names) {
        Zone z;
        z.id = model_.GenerateZoneId();
        z.name = name;
        z.monitorIndex = monitorIndex;
        z.rect = RECT{x, y, x + 340, y + 240};
        x += 380;
        if (x > 1400) { x = 40; y += 280; }
        model_.AddZone(std::move(z));
    }

    for (const auto& icon : icons) {
        if (icon.path.empty()) continue;
        std::wstring category = ClassifyPath(icon.path);
        Zone* zone = model_.FindZoneByName(category);
        if (!zone) continue;
        zone->itemPaths.push_back(icon.path);
        // 只记录移动前的真实位置；重分类时旧图标已在屏幕外，记下 -32000 会导致原位永久丢失
        if (icon.position.x > kOffscreenCoord && icon.position.y > kOffscreenCoord) {
            model_.Layout().originalIconPositions[icon.path] = icon.position;
        }
        iconManager_->MoveIconOffscreen(icon.index);
    }
    DebugLog(config_->GetRootDir(), L"AutoClassify done items=" + std::to_wstring(CountZoneItems()));
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
                                                zone, iconService_.get(),
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
            if (PromptText(owner, L"重命名分区", newName) && !newName.empty()) {
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
            DebugLog(config_->GetRootDir(), L"ZoneWindow create FAILED: " + zone.name);
            continue;
        }
        DebugLog(config_->GetRootDir(),
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
            DebugLog(config_->GetRootDir(),
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
            DebugLog(config_->GetRootDir(),
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
            DebugLog(config_->GetRootDir(), L"ZoneWindow fallback bottom: " + zone.name);
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
    if (iconManager_) iconManager_->HideAllIcons(cleanMode_);
}

void DesktopWorkspace::RestoreDesktop() {
    if (!iconManager_) return;
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
    DebugLog(config_->GetRootDir(), L"RestoreDesktop restored=" + std::to_wstring(restored));
}

void DesktopWorkspace::SaveLayout() {
    model_.Save(layoutPath_);
}

void DesktopWorkspace::StartDesktopWatcher() {
    PWSTR desktopPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktopPath))) return;

    // watcher 回调在后台线程触发，这里只投递消息，实际收集逻辑封送回 UI 线程执行，
    // 避免后台线程与交互操作并发读写 layout 数据
    desktopWatcher_ = std::make_unique<DirectoryWatcher>(desktopPath, [this]() {
        if (msgHwnd_) PostMessageW(msgHwnd_, kWatcherNotifyMsg, 0, 0);
    });
    desktopWatcher_->Start();
    CoTaskMemFree(desktopPath);
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
        std::wstring category = ClassifyPath(icon.path);
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

void DesktopWorkspace::StartMouseHook() {
    if (mouseHook_) return;
    g_mouseHookWorkspace = this;
    mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, MouseHookProc,
                                   GetModuleHandleW(L"DesktopSticker.Features.dll"), 0);
}

void DesktopWorkspace::StopMouseHook() {
    if (mouseHook_) {
        UnhookWindowsHookEx(mouseHook_);
        mouseHook_ = nullptr;
    }
    if (g_mouseHookWorkspace == this) g_mouseHookWorkspace = nullptr;
}

LRESULT CALLBACK DesktopWorkspace::MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        auto* self = g_mouseHookWorkspace;
        if (self && info) {
            const POINT pt = info->pt;
            ZoneWindow* zone = self->ZoneAtPoint(pt); // 已过滤隐藏分区（干净桌面不拦截）
            if (zone && !zone->IsEmbedded()) {
                // 降级模式（分区未嵌入桌面、处在最底层）：系统把输入路由给上层窗口，
                // 这里转发给分区并吞掉原始事件；嵌入模式下系统直接命中分区子窗口，
                // 绝不能重复转发，否则每次点击被处理两次（双击标题会折叠又展开）
                const UINT msg = static_cast<UINT>(wParam);
                if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_RBUTTONUP) {
                    return self->ForwardMouseToZone(zone, msg, pt);
                }
            } else if (!zone && wParam == WM_LBUTTONDOWN) {
                self->DetectDesktopBlankDoubleClick(pt); // 双击桌面空白 → 干净桌面
            }
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT DesktopWorkspace::ForwardMouseToZone(ZoneWindow* zone, UINT msg, const POINT& pt) {
    // 系统只为命中窗口合成 WM_LBUTTONDBLCLK，最底层分区收不到，双击在这里手动合成
    bool dblclk = false;
    if (msg == WM_LBUTTONDOWN) {
        const DWORD now = GetTickCount();
        if (forwardDownZone_ == zone &&
            now - forwardDownTick_ <= GetDoubleClickTime() &&
            std::abs(pt.x - forwardDownPt_.x) <= GetSystemMetrics(SM_CXDOUBLECLK) / 2 &&
            std::abs(pt.y - forwardDownPt_.y) <= GetSystemMetrics(SM_CYDOUBLECLK) / 2) {
            dblclk = true;
            forwardDownZone_ = nullptr;
        } else {
            forwardDownZone_ = zone;
            forwardDownTick_ = now;
            forwardDownPt_ = pt;
        }
    }
    POINT client = pt;
    ScreenToClient(zone->Hwnd(), &client);
    WPARAM wp = 0;
    if (msg == WM_LBUTTONDOWN || dblclk) wp = MK_LBUTTON;
    if (msg == WM_RBUTTONUP) wp = MK_RBUTTON;
    // PostMessage 而非 SendMessage：绝不能在 hook 回调里弹菜单/做 IO，
    // 否则超过 LowLevelHooksTimeout 后系统会静默摘除 hook，之后所有转发失效
    PostMessageW(zone->Hwnd(), dblclk ? WM_LBUTTONDBLCLK : msg, wp, MAKELPARAM(client.x, client.y));
    return 1; // 吞掉原始事件，避免桌面同时响应
}

void DesktopWorkspace::DetectDesktopBlankDoubleClick(const POINT& pt) {
    const DWORD now = GetTickCount();
    const bool dblclk =
        blankDownTick_ != 0 &&
        now - blankDownTick_ <= GetDoubleClickTime() &&
        std::abs(pt.x - blankDownPt_.x) <= GetSystemMetrics(SM_CXDOUBLECLK) / 2 &&
        std::abs(pt.y - blankDownPt_.y) <= GetSystemMetrics(SM_CYDOUBLECLK) / 2;
    blankDownTick_ = now;
    blankDownPt_ = pt;
    if (!dblclk) return;

    // 确认双击发生在桌面层（Progman/WorkerW/listView），而不是其他应用窗口上
    HWND hit = WindowFromPoint(pt);
    if (!hit) return;
    const HWND root = GetAncestor(hit, GA_ROOT);
    const auto& w = shell_.Windows();
    if (root != w.workerw && root != w.progman && hit != w.listView) return;

    if (cleanMode_) {
        ToggleCleanDesktop(); // 干净桌面模式：双击空白处恢复分区
        return;
    }
    if (!w.listView || !IsWindow(w.listView) || !IsWindowVisible(w.listView)) return;

    // LVM_HITTEST 的 LVHITTESTINFO* 必须位于 Explorer 进程内存，否则 Explorer 崩溃
    DWORD pid = 0;
    GetWindowThreadProcessId(w.listView, &pid);
    HANDLE hProc = pid ? OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, FALSE, pid) : nullptr;
    int index = -1;
    if (hProc) {
        LPVOID remote = VirtualAllocEx(hProc, nullptr, sizeof(LVHITTESTINFO),
                                       MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (remote) {
            POINT client = pt;
            ScreenToClient(w.listView, &client);
            LVHITTESTINFO ht{};
            ht.pt = client;
            WriteProcessMemory(hProc, remote, &ht, sizeof(ht), nullptr);
            DWORD_PTR hitResult = 0;
            SendMessageTimeoutW(w.listView, LVM_HITTEST, 0, reinterpret_cast<LPARAM>(remote),
                                SMTO_ABORTIFHUNG, 500, &hitResult);
            ReadProcessMemory(hProc, remote, &ht, sizeof(ht), nullptr);
            index = ht.iItem;
            VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
        }
        CloseHandle(hProc);
    }
    if (index == -1) ToggleCleanDesktop(); // 双击空白处（不是图标）
}

} // namespace desktopsticker
