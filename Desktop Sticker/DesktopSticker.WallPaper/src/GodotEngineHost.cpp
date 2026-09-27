#include "pch.h"
#include "GodotEngineHost.h"

#include "Log.h"
#include "Utf8.h"

#include <filesystem>
#include <vector>

namespace desktopsticker::wallpaper {

namespace {

namespace fs = std::filesystem;

constexpr wchar_t kRuntimeDllName[] = L"godot.windows.template_release.x86_64.dll";
constexpr wchar_t kWndClass[] = L"DstkGodotWallpaper";

fs::path exe_dir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return fs::path(path).parent_path();
}

// 运行库位置：<exeDir>\godot\godot.windows...dll；找不到就扫同目录里第一个 godot*.dll
std::wstring find_runtime_dll() {
    std::error_code ec;
    const fs::path dir = exe_dir() / L"godot";
    const fs::path exact = dir / kRuntimeDllName;
    if (fs::is_regular_file(exact, ec)) return exact.wstring();
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        const std::wstring name = entry.path().filename().wstring();
        if (name.rfind(L"godot", 0) == 0 && entry.path().extension() == L".dll") {
            return entry.path().wstring();
        }
    }
    return {};
}

LRESULT CALLBACK host_wnd_proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    // 与壁纸窗口同款：点击穿透（HTTRANSPARENT），不做样式/背景处理。
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (msg == WM_ERASEBKGND) return 1;
    return DefWindowProcW(h, msg, w, l);
}

bool register_window_class() {
    static bool registered = false;
    if (registered) return true;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = host_wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWndClass;
    registered = RegisterClassExW(&wc) != 0;
    if (!registered) {
        wp_log("godot host: RegisterClassExW failed: " + std::to_string(GetLastError()));
    }
    return registered;
}

// 场景壁纸结束后，Explorer 承载壁纸的 WorkerW 空置会露白：
// 重设一次注册表里的壁纸路径（内容不变）强制重绘。
void restore_desktop_wallpaper() {
    wchar_t path[MAX_PATH]{};
    DWORD size = sizeof(path);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"WallPaper",
                     RRF_RT_REG_SZ, nullptr, path, &size) == ERROR_SUCCESS &&
        path[0] != L'\0') {
        SystemParametersInfoW(0x14 /* SPI_SETDESKWALLPAPER */, 0, path,
                              0x3 /* SPIF_UPDATEINIFILE | SPIF_SENDCHANGE */);
        wp_log("godot host: desktop wallpaper restored");
    }
}

} // namespace

GodotEngineHost& GodotEngineHost::instance() {
    static GodotEngineHost host;
    return host;
}

bool GodotEngineHost::runtime_available() const {
    return !find_runtime_dll().empty();
}

bool GodotEngineHost::ensure_runtime(std::string& error) {
    if (dll_) return true;

    const std::wstring path = find_runtime_dll();
    if (path.empty()) {
        error = "缺少 Godot 运行库（godot\\" + to_utf8(kRuntimeDllName) + "）";
        return false;
    }
    dll_ = LoadLibraryExW(path.c_str(), nullptr,
                          LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dll_) {
        error = "加载 Godot 运行库失败（错误码 " + std::to_string(GetLastError()) + "）";
        wp_log("godot host: LoadLibraryExW failed: " + to_utf8(path));
        return false;
    }

    fn_set_present_ = reinterpret_cast<decltype(fn_set_present_)>(GetProcAddress(dll_, "dstk_embed_set_present_hwnd"));
    fn_create_ = reinterpret_cast<decltype(fn_create_)>(GetProcAddress(dll_, "dstk_godot_create"));
    fn_start_ = reinterpret_cast<decltype(fn_start_)>(GetProcAddress(dll_, "dstk_godot_start"));
    fn_iteration_ = reinterpret_cast<decltype(fn_iteration_)>(GetProcAddress(dll_, "dstk_godot_iteration"));
    fn_pause_ = reinterpret_cast<decltype(fn_pause_)>(GetProcAddress(dll_, "dstk_godot_pause"));
    fn_resume_ = reinterpret_cast<decltype(fn_resume_)>(GetProcAddress(dll_, "dstk_godot_resume"));
    fn_destroy_ = reinterpret_cast<decltype(fn_destroy_)>(GetProcAddress(dll_, "dstk_godot_destroy"));
    fn_load_pack_ = reinterpret_cast<decltype(fn_load_pack_)>(GetProcAddress(dll_, "dstk_godot_load_pack"));
    if (!fn_set_present_ || !fn_create_ || !fn_start_ || !fn_iteration_ || !fn_destroy_ || !fn_load_pack_) {
        error = "Godot 运行库缺少嵌入接口（版本不匹配？）";
        wp_log("godot host: runtime exports missing");
        FreeLibrary(dll_);
        dll_ = nullptr;
        return false;
    }
    wp_log("godot host: runtime loaded: " + to_utf8(path));
    return true;
}

bool GodotEngineHost::ensure_window(HWND parent, int width, int height, std::string& error) {
    if (window_ && IsWindow(window_)) return true;
    if (!register_window_class()) {
        error = "注册壁纸窗口类失败";
        return false;
    }

    parent_ = parent;
    const DWORD style = parent ? WS_CHILD : WS_POPUP;
    window_ = CreateWindowExW(WS_EX_NOACTIVATE, kWndClass, L"DstkGodotWallpaper", style,
                              0, 0, width, height, parent, nullptr,
                              GetModuleHandleW(nullptr), nullptr);
    if (!window_) {
        error = "创建壁纸窗口失败（错误码 " + std::to_string(GetLastError()) + "）";
        parent_ = nullptr;
        return false;
    }
    wp_log("godot host: window created " + std::to_string(width) + "x" + std::to_string(height) +
           (parent ? " (embedded in WorkerW)" : " (top-level fallback)"));
    return true;
}

bool GodotEngineHost::ensure_instance(const std::wstring& packPath, const std::string& mainScene, std::string& error) {
    if (instance_) {
        if (packPath == currentPack_) return true;
        // 换包：挂载新 PCK 并切主场景（旧包保持挂载，内存会累积，v1 接受）。
        if (!fn_load_pack_(instance_, to_utf8(packPath).c_str(), mainScene.c_str())) {
            error = "切换壁纸包失败（无法挂载 project.pck）";
            wp_log("godot host: dstk_godot_load_pack failed");
            return false;
        }
        currentPack_ = packPath;
        currentScene_ = mainScene;
        wp_log("godot host: pack switched: " + to_utf8(packPath));
        return true;
    }

    // 首次：引擎只能在隐藏窗口上完成初始注册（AccessKit 子类化适配器的硬要求），
    // 所以顺序固定为 建窗口(隐藏) -> 建实例 -> 启动 -> 显示。
    fn_set_present_(window_);

    char exePath[MAX_PATH]{};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    const std::string resArg = std::to_string(width_) + "x" + std::to_string(height_);
    std::vector<std::string> args = {
        exePath,
        "--main-pack", to_utf8(packPath),
        "--resolution", resArg,
        "--", "--dstk-embedded",
    };
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (auto& a : args) argv.push_back(a.data());

    instance_ = fn_create_(static_cast<int>(argv.size()), argv.data());
    if (!instance_) {
        error = "创建 Godot 实例失败（壁纸包损坏或与运行库版本不符）";
        wp_log("godot host: dstk_godot_create failed");
        destroy_all();
        return false;
    }
    if (!fn_start_(instance_)) {
        error = "启动 Godot 实例失败";
        wp_log("godot host: dstk_godot_start failed");
        destroy_all();
        return false;
    }
    currentPack_ = packPath;
    currentScene_ = mainScene;
    wp_log("godot host: instance started (pack=" + to_utf8(packPath) + ")");
    return true;
}

bool GodotEngineHost::activate(const std::wstring& packPath, const std::string& mainScene,
                               HWND parent, int width, int height, std::string& error) {
    error.clear();
    lastError_.clear();
    if (packPath.empty() || mainScene.empty()) {
        error = "壁纸包参数不完整";
        return false;
    }
    if (width <= 0 || height <= 0) {
        error = "壁纸尺寸无效";
        return false;
    }

    width_ = width;
    height_ = height;

    if (!ensure_runtime(error) || !ensure_window(parent, width, height, error) ||
        !ensure_instance(packPath, mainScene, error)) {
        lastError_ = error;
        return false;
    }

    // 激活：置顶（同层 sibling 里）并显示。壁纸不抢焦点。
    SetWindowPos(window_, HWND_TOP, 0, 0, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    active_ = true;
    notified_paused_ = false;
    if (fn_resume_) fn_resume_(instance_);
    return true;
}

void GodotEngineHost::deactivate() {
    active_ = false;
    if (window_ && IsWindow(window_)) {
        ShowWindow(window_, SW_HIDE);
    }
}

void GodotEngineHost::tick() {
    if (!active_ || !instance_) return;
    // GodotInstance::iteration() 返回 true = 引擎请求退出（与 OS_Windows::run 同语义）。
    if (fn_iteration_(instance_)) {
        wp_log("godot host: engine requested exit; deactivating scene wallpaper");
        deactivate();
    }
}

void GodotEngineHost::notify_paused(bool paused) {
    if (!instance_) return;
    if (paused == notified_paused_) return;
    notified_paused_ = paused;
    if (paused) {
        if (fn_pause_) fn_pause_(instance_);
    } else if (fn_resume_) {
        fn_resume_(instance_);
    }
}

void GodotEngineHost::destroy_all() {
    if (instance_) {
        fn_destroy_(instance_);   // 内部 Main::cleanup，会连带销毁它持有的窗口
        instance_ = nullptr;
    }
    if (window_ && IsWindow(window_)) {
        DestroyWindow(window_);
    } else {
        // 引擎清理时可能已经销毁了窗口；句柄失效仅计数用。
    }
    window_ = nullptr;
}

void GodotEngineHost::shutdown() {
    const bool was_embedded = parent_ != nullptr;
    destroy_all();
    parent_ = nullptr;
    currentPack_.clear();
    currentScene_.clear();
    active_ = false;
    notified_paused_ = false;

    if (dll_) {
        FreeLibrary(dll_);
        dll_ = nullptr;
    }
    fn_set_present_ = nullptr;
    fn_create_ = nullptr;
    fn_start_ = nullptr;
    fn_iteration_ = nullptr;
    fn_pause_ = nullptr;
    fn_resume_ = nullptr;
    fn_destroy_ = nullptr;
    fn_load_pack_ = nullptr;

    if (was_embedded) {
        restore_desktop_wallpaper();
    }
}

} // namespace desktopsticker::wallpaper
