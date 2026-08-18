#include "pch.h"
#include "desktopsticker/DesktopWorkspace.h"

#include <windowsx.h>
#include <commctrl.h>
#include <cstring>

#include "desktopsticker/Utf8.h"

namespace fs = std::filesystem;

namespace desktopsticker {

namespace {

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
        OleInitialize(nullptr);

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

        // 记录并关闭自动排列，避免被收纳图标自动回位
        if (iconManager_->ListView()) {
            LONG_PTR style = GetWindowLongPtrW(iconManager_->ListView(), GWL_STYLE);
            model_.Layout().autoArrangeWasEnabled = (style & LVS_AUTOARRANGE) != 0;
            iconManager_->SetAutoArrange(false);
            DebugLog(root, L"listview found");
        } else {
            DebugLog(root, L"listview NOT found");
        }

        // 已有布局则加载；否则首次自动分类
        if (!model_.Load(layoutPath_) || model_.Layout().zones.empty()) {
            auto icons = iconManager_->EnumIcons();
            AutoClassify(icons);
            SaveLayout();
        }
        DebugLog(root, L"zones=" + std::to_wstring(model_.Layout().zones.size()));

        // 把已收纳的原生图标移到屏幕外（启动恢复场景）
        for (const auto& zone : model_.Layout().zones) {
            for (const auto& path : zone.itemPaths) {
                auto icons = iconManager_->EnumIcons();
                for (const auto& icon : icons) {
                    if (_wcsicmp(icon.path.c_str(), path.c_str()) == 0) {
                        iconManager_->MoveIconOffscreen(icon.index);
                        break;
                    }
                }
            }
        }

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
    desktopWatcher_.reset();
    StopMouseHook();
    DestroyZoneWindows();
    RestoreDesktop();
    iconService_->ClearCache();
    iconManager_.reset();
    shell_.Shutdown();
    OleUninitialize();
}

void DesktopWorkspace::Refresh() {
    for (auto& w : zoneWindows_) w->Refresh();
    if (zonesChanged_) zonesChanged_();
}

std::wstring DesktopWorkspace::ClassifyPath(const std::wstring& path) {
    fs::path p(path);
    std::wstring ext = p.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

    if (ext == L".lnk" || ext == L".exe" || ext == L".appref-ms") return L"应用";
    if (ext == L".jpg" || ext == L".jpeg" || ext == L".png" || ext == L".gif" || ext == L".bmp" || ext == L".webp") return L"图片";
    if (ext == L".doc" || ext == L".docx" || ext == L".pdf" || ext == L".txt" || ext == L".md" || ext == L".xls" || ext == L".xlsx" || ext == L".ppt" || ext == L".pptx") return L"文档";
    if (ext == L".mp4" || ext == L".avi" || ext == L".mkv" || ext == L".mov" || ext == L".wmv") return L"视频";
    if (ext == L".mp3" || ext == L".wav" || ext == L".flac" || ext == L".aac") return L"音乐";
    if (fs::is_directory(p)) return L"文件夹";
    return L"其他";
}

void DesktopWorkspace::AutoClassify(const std::vector<DesktopIconInfo>& icons) {
    const wchar_t* names[] = {L"应用", L"文档", L"图片", L"视频", L"音乐", L"文件夹", L"其他"};
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
        Zone* zone = model_.FindZone(category);
        if (!zone) continue;
        zone->itemPaths.push_back(icon.path);
        model_.Layout().originalIconPositions[icon.path] = icon.position;
        iconManager_->MoveIconOffscreen(icon.index);
    }
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
            if (model_.Layout().originalIconPositions.find(path) == model_.Layout().originalIconPositions.end()) {
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
            auto posIt = model_.Layout().originalIconPositions.find(path);
            if (posIt != model_.Layout().originalIconPositions.end()) {
                iconManager_->RestoreIcon(icon.index, posIt->second);
            }
        }
    }
    SaveLayout();
    Refresh();
}

void DesktopWorkspace::CreateZoneWindows() {
    ZoneWindow::RegisterClass(GetModuleHandleW(L"DesktopSticker.Features.dll"));
    for (const auto& zone : model_.Layout().zones) {
        auto win = std::make_unique<ZoneWindow>(GetModuleHandleW(L"DesktopSticker.Features.dll"),
                                                zone, iconService_.get());

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

        win->onItemDrag = [this](const std::wstring& fromZoneId, const std::wstring& itemPath) {
            // MVP：拖出即移回桌面；完整跨分区拖放由 DropTarget 扩展
            RemoveFromZone(fromZoneId, itemPath);
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
            DebugLog(config_->GetRootDir(),
                     L"  after SetParent parent=" +
                     std::to_wstring(reinterpret_cast<uintptr_t>(GetParent(win->Hwnd()))) +
                     L" ancestor=" +
                     std::to_wstring(reinterpret_cast<uintptr_t>(GetAncestor(win->Hwnd(), GA_PARENT))));
            SetWindowPos(win->Hwnd(), HWND_TOP, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
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
    }
}

void DesktopWorkspace::DestroyZoneWindows() {
    for (auto* drop : dropTargets_) {
        if (drop) {
            RevokeDragDrop(drop->Hwnd());
            drop->Release();
        }
    }
    dropTargets_.clear();
    zoneWindows_.clear();
    ZoneWindow::UnregisterClass(GetModuleHandleW(L"DesktopSticker.Features.dll"));
}

void DesktopWorkspace::ToggleCleanDesktop() {
    cleanMode_ = !cleanMode_;
    for (auto& w : zoneWindows_) {
        ShowWindow(w->Hwnd(), cleanMode_ ? SW_HIDE : SW_SHOW);
    }
    auto icons = iconManager_->EnumIcons();
    for (const auto& icon : icons) {
        if (cleanMode_) {
            iconManager_->MoveIconOffscreen(icon.index);
        } else {
            auto it = model_.Layout().originalIconPositions.find(icon.path);
            if (it != model_.Layout().originalIconPositions.end()) {
                iconManager_->RestoreIcon(icon.index, it->second);
            }
        }
    }
}

void DesktopWorkspace::RestoreDesktop() {
    if (!iconManager_) return;
    auto icons = iconManager_->EnumIcons();
    for (const auto& icon : icons) {
        auto it = model_.Layout().originalIconPositions.find(icon.path);
        if (it != model_.Layout().originalIconPositions.end()) {
            iconManager_->RestoreIcon(icon.index, it->second);
        }
    }
    if (model_.Layout().autoArrangeWasEnabled) {
        iconManager_->SetAutoArrange(true);
    }
}

void DesktopWorkspace::SaveLayout() {
    model_.Save(layoutPath_);
}

void DesktopWorkspace::StartDesktopWatcher() {
    PWSTR desktopPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktopPath))) return;

    desktopWatcher_ = std::make_unique<DirectoryWatcher>(desktopPath, [this]() {
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
            Zone* zone = model_.FindZone(category);
            if (zone) {
                zone->itemPaths.push_back(icon.path);
                model_.Layout().originalIconPositions[icon.path] = icon.position;
                iconManager_->MoveIconOffscreen(icon.index);
            }
        }
        SaveLayout();
        Refresh();
    });
    desktopWatcher_->Start();
    CoTaskMemFree(desktopPath);
}

bool DesktopWorkspace::IsPointOverZone(POINT pt) const {
    for (const auto& w : zoneWindows_) {
        if (!w || !w->Hwnd()) continue;
        RECT rc{};
        if (GetWindowRect(w->Hwnd(), &rc) && PtInRect(&rc, pt)) return true;
    }
    return false;
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
    if (nCode == HC_ACTION && wParam == WM_LBUTTONDBLCLK) {
        auto* self = g_mouseHookWorkspace;
        if (self) {
            POINT pt{};
            GetCursorPos(&pt);
            if (!self->IsPointOverZone(pt)) {
                HWND lv = self->shell_.Windows().listView;
                if (lv && IsWindow(lv)) {
                    RECT rc{};
                    GetWindowRect(lv, &rc);
                    if (PtInRect(&rc, pt)) {
                        POINT client = pt;
                        ScreenToClient(lv, &client);
                        LVHITTESTINFO ht{};
                        ht.pt = client;
                        const int index = static_cast<int>(
                            SendMessageW(lv, LVM_HITTEST, 0, reinterpret_cast<LPARAM>(&ht)));
                        if (index == -1) { // 桌面空白处
                            self->ToggleCleanDesktop();
                        }
                    }
                }
            }
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

} // namespace desktopsticker
