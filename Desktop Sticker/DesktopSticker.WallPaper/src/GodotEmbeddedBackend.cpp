#include "pch.h"
#include "GodotEmbeddedBackend.h"

#include "GodotEngineHost.h"
#include "Log.h"
#include "Utf8.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "desktopsticker/wallpaper/WallPaperPackage.h"

namespace desktopsticker::wallpaper {

namespace {

namespace fs = std::filesystem;

bool read_text_file(const fs::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

} // namespace

bool GodotEmbeddedBackend::Open(const BackendRequest& request, const BackendContext& ctx) {
    // 已解包的壁纸包：sourcePath 是 media/<id>/shader，里面是 project.pck + manifest.json。
    const fs::path dir(request.sourcePath);
    const fs::path packPath = dir / kPackEntryPack;
    const fs::path manifestPath = dir / kPackEntryManifest;

    std::error_code ec;
    if (!fs::is_regular_file(packPath, ec)) {
        wp_log("godot backend: project.pck missing in " + to_utf8(request.sourcePath));
        return false;
    }

    std::string manifestText;
    if (!read_text_file(manifestPath, manifestText)) {
        wp_log("godot backend: manifest.json missing in " + to_utf8(request.sourcePath));
        return false;
    }
    WallPaperManifest manifest;
    std::string parseError;
    if (!parse_wallpaper_manifest(manifestText, manifest, parseError)) {
        wp_log("godot backend: manifest invalid: " + parseError);
        return false;
    }

    // 宿主已经挂进桌面层：把它所在 WorkerW 交给引擎自建的呈现窗口；
    // 若宿主退化为顶层窗口（WorkerW 不可用），引擎也用顶层窗口兜底。
    HWND parent = nullptr;
    if (ctx.window) {
        const HWND candidate = GetAncestor(ctx.window, GA_PARENT);
        if (candidate && candidate != GetDesktopWindow()) {
            parent = candidate;
        }
    }

    std::string error;
    if (!GodotEngineHost::instance().activate(packPath.wstring(), manifest.mainScene,
                                              parent, ctx.width, ctx.height, error)) {
        wp_log("godot backend: activate failed: " + error);
        return false;
    }

    serial_ = 0;

    // 场景壁纸接交互：把壁纸窗口登记成鼠标接收方（钩子由模块统一持有）
    relay_ = ctx.mouse;
    if (relay_) relay_->Attach(ctx.window, this);

    wp_log("godot backend opened: " + to_utf8(request.sourcePath) +
           " (main_scene=" + manifest.mainScene + ")");
    return true;
}

void GodotEmbeddedBackend::Close() {
    if (relay_) {
        relay_->Detach(this);
        relay_ = nullptr;
    }
    // 只停迭代并隐藏窗口；引擎与窗口保留（进程级单例，切换壁纸时复用）。
    GodotEngineHost::instance().deactivate();
}

void GodotEmbeddedBackend::Tick() {
    GodotEngineHost::instance().tick();
    ++serial_;
}

void GodotEmbeddedBackend::SetPaused(bool paused) {
    GodotEngineHost::instance().notify_paused(paused);
}

// 渲染线程上的鼠标事件 → 引擎（Input::parse_input_event）。
void GodotEmbeddedBackend::OnRelayMouse(const MouseRelayEvent& event) {
    GodotEngineHost::instance().send_mouse(event);
}

} // namespace desktopsticker::wallpaper
