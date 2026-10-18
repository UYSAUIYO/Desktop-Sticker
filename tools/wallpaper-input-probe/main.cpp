// 壁纸鼠标交互的端到端探针。
//
// 只加载 DesktopSticker.WallPaper.dll、Init/Start 播放配置文件里的当前壁纸，
// 不启动桌面贴纸的 UI（没有分区卡片、没有设置窗口），所以整个桌面只有壁纸本身——
// 用来验证"低级钩子 → 后端转发"这条链路是否真的把鼠标送到了壁纸里。
//
// 用法：
//   dstk-wallpaper-probe.exe [--dll <壁纸 DLL>] [--seconds 15]
//
// 交互由外部注入：SetCursorPos / mouse_event（会经过 WH_MOUSE_LL 钩子，与真实鼠标同路）。
// 退出时调用 Shutdown（会恢复桌面壁纸）。

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "desktopsticker/IWallPaperModule.h"

using CreateFn = desktopsticker::IWallPaperModule* (*)();
using DestroyFn = void (*)(desktopsticker::IWallPaperModule*);

static std::string exe_dir() {
    char path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string dir(path);
    const size_t slash = dir.find_last_of("\\/");
    return slash == std::string::npos ? "." : dir.substr(0, slash);
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);

    std::string dll_path = exe_dir() + "\\DesktopSticker.WallPaper.dll";
    double seconds = 15.0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--dll") == 0 && i + 1 < argc) {
            dll_path = argv[++i];
        } else if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            seconds = std::atof(argv[++i]);
        } else {
            std::printf("[probe] unknown argument: %s\n", argv[i]);
            return 2;
        }
    }

    HMODULE dll = LoadLibraryA(dll_path.c_str());
    if (!dll) {
        std::printf("[probe] FATAL: LoadLibrary failed (%lu): %s\n", GetLastError(), dll_path.c_str());
        return 1;
    }
    auto create = reinterpret_cast<CreateFn>(GetProcAddress(dll, "CreateWallPaperModule"));
    auto destroy = reinterpret_cast<DestroyFn>(GetProcAddress(dll, "DestroyWallPaperModule"));
    if (!create || !destroy) {
        std::printf("[probe] FATAL: exports missing in %s\n", dll_path.c_str());
        return 1;
    }

    desktopsticker::IWallPaperModule* module = create();
    if (!module) {
        std::printf("[probe] FATAL: CreateWallPaperModule returned null\n");
        return 1;
    }

    desktopsticker::WallPaperEvents events;
    if (!module->Init(events)) {
        std::printf("[probe] FATAL: Init failed\n");
        destroy(module);
        return 1;
    }
    if (!module->Start()) {
        std::printf("[probe] FATAL: Start failed (library root / storage record?)\n");
        module->Shutdown();
        destroy(module);
        return 1;
    }

    const auto settings = module->GetSettings();
    std::printf("[probe] started: activeId=%ls enabled=%d interactive=%d\n",
                settings.activeId.c_str(), settings.enabled ? 1 : 0,
                settings.interactiveInput ? 1 : 0);

    const DWORD started = GetTickCount();
    while ((GetTickCount() - started) < static_cast<DWORD>(seconds * 1000.0)) {
        Sleep(500);
        const auto st = module->PlaybackStatus();
        std::printf("[probe] backend=%ls playing=%d fps=%.1f %dx%d\n", st.backend.c_str(),
                    st.playing ? 1 : 0, st.fps, st.width, st.height);
    }

    std::printf("[probe] shutting down\n");
    module->Stop();
    module->Shutdown();
    destroy(module);
    FreeLibrary(dll);
    std::printf("[probe] exit\n");
    return 0;
}
