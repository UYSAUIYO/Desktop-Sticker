// DesktopSticker Wallpaper Studio —— 宿主进程。
//
// 职责刻意保持极小：加载 fork 出的 libgodot、把 studio 工程交给它、驱动迭代。
// 编辑器的一切（界面、文档、导出）都在 <exeDir>\studio\ 那个 Godot 工程里。
//
// 与 tools/godot-host-test 的关键区别：这里**不**调用 dstk_embed_set_present_hwnd，
// 引擎自建顶层窗口 —— Studio 是一个普通桌面应用，不是挂在 WorkerW 上的壁纸。
//
// 为什么 Studio 必须以“目录”形式分发：导出流程要在 res:// 下生成场景再 ResourceSaver
// 保存，而打成 pck 时 res:// 是只读的（导出会直接失败）。见设计 §2.2。

#include <windows.h>
#include <dbghelp.h>
#include <shlobj.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "shell32.lib")

namespace {

using CreateFn = void *(*)(int, char **);
using BoolFn = bool (*)(void *);
using VoidFn = void (*)(void *);

std::wstring exe_dir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path);
    const size_t slash = dir.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring(L".") : dir.substr(0, slash);
}

bool file_exists(const std::wstring &p) {
    const DWORD attrs = GetFileAttributesW(p.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::string to_utf8(const std::wstring &s) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(n) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, out.data(), n, nullptr, nullptr);
    return out;
}

// 用 UTF-8 而不是 %ls 写 stderr：控制台代码页不是 UTF-8 时 %ls 会全变成问号
// （实测 “Godot 实例创建失败” 变成 “Godot ???????”），日志就没法看了。
void fatal(const std::wstring &message) {
    const std::string utf8 = to_utf8(message);
    std::fprintf(stderr, "[studio] FATAL: %s\n", utf8.c_str());
    std::fflush(stderr);
    MessageBoxW(nullptr, message.c_str(), L"桌面贴纸 · 壁纸编辑器", MB_ICONERROR | MB_OK);
}

// Godot 的 Main::setup 收 char**（非 const）。这些字符串在整个调用期间都活着，
// 且引擎不会真的改写它们 —— 与 tools/godot-host-test/main.cpp 的做法一致。
char *path_utf8(std::string &s) {
    return s.empty() ? const_cast<char *>("") : &s[0];
}

// ---------------------------------------------------------------- Vulkan 环境隔离

// 第三方 Vulkan “隐式层”会被 loader 无条件注入每个 Vulkan 进程，即使对应程序没在跑。
// 本机实测：dstk-studio 里加载了 C:\ProgramData\obs-studio-hook\graphics-hook64.dll
// （OBS 的抓帧层，注册表 ImplicitLayers 里只有 disable_environment、没有
// enable_environment，所以恒生效），而 OBS 根本没开。这类层在渲染线程上做 inline
// hook，是卡死/掉帧的常见来源，而编辑器不需要被抓帧。
//
// 在创建实例之前设环境变量（loader 在 vkCreateInstance 时才读）：
//   VK_LOADER_LAYERS_DISABLE=~implicit~   关掉全部隐式层（loader >= 1.3.234；本机 1.4.321）
//   DISABLE_VULKAN_OBS_CAPTURE=1          老版本 OBS 层只认这个开关
void shield_from_vulkan_overlays() {
    SetEnvironmentVariableW(L"VK_LOADER_LAYERS_DISABLE", L"~implicit~");
    SetEnvironmentVariableW(L"DISABLE_VULKAN_OBS_CAPTURE", L"1");
}

// ---------------------------------------------------------------- 卡顿自捕获
//
// 用户报的“运行一会就卡死”在没有符号的运行库里只能靠现场 dump 定位。外部看门狗
// 只能等窗口被系统标成“未响应”（≥5s）之后才动，而且进程一关证据就没了，所以宿主
// 自己盯：主循环每帧记一次开始时间，另开一个线程看它多久没更新；超过 3 秒就写一份
// minidump + 一行日志到 %LOCALAPPDATA%\DesktopSticker\，进程照常继续跑。
//
// 只写前几份（每次启动最多 3 份），避免真出问题时把磁盘塞满。

std::atomic<unsigned long long> g_iter_start_us{0}; // 0 = 不在帧内
std::atomic<unsigned long long> g_iter_count{0};
std::atomic<bool> g_watch_stop{false};

unsigned long long now_us() {
    LARGE_INTEGER f{}, c{};
    ::QueryPerformanceFrequency(&f);
    ::QueryPerformanceCounter(&c);
    return static_cast<unsigned long long>(c.QuadPart * 1000000ull / f.QuadPart);
}

std::wstring stall_dir() {
    wchar_t *base = nullptr;
    std::wstring dir;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base)) && base) {
        dir = std::wstring(base) + L"\\DesktopSticker";
        ::CoTaskMemFree(base);
    } else {
        dir = L".";
    }
    dir += L"\\stalls";
    ::CreateDirectoryW((std::wstring(dir).substr(0, dir.find_last_of(L'\\'))).c_str(), nullptr);
    ::CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

void append_line(const std::wstring &path, const std::wstring &line) {
    HANDLE f = ::CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        return;
    }
    std::string utf8 = to_utf8(line + L"\r\n");
    DWORD written = 0;
    ::WriteFile(f, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    ::CloseHandle(f);
}

void write_stall_dump(const std::wstring &dir, unsigned long long stuck_ms, unsigned long long frames) {
    SYSTEMTIME st{};
    ::GetLocalTime(&st);
    wchar_t stamp[64]{};
    swprintf_s(stamp, L"%02d%02d%02d_%03d", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    wchar_t name[128]{};
    swprintf_s(name, L"stall_%u_%s.dmp", ::GetCurrentProcessId(), stamp);

    const std::wstring path = dir + L"\\" + name;
    HANDLE f = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        return;
    }
    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo |
                                                 MiniDumpWithHandleData | MiniDumpWithUnloadedModules);
    ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), f, type, nullptr, nullptr, nullptr);
    ::CloseHandle(f);

    wchar_t line[512]{};
    swprintf_s(line, L"%s  pid=%u  frame=%llu  stuck=%llums  dump=%s", stamp,
               ::GetCurrentProcessId(), frames, stuck_ms, name);
    append_line(dir + L"\\stalls.log", line);
}

DWORD WINAPI stall_watch_thread(LPVOID) {
    const std::wstring dir = stall_dir();
    int dumps = 0;
    unsigned long long last_report = 0;
    while (!g_watch_stop.load()) {
        ::Sleep(400);
        const unsigned long long started = g_iter_start_us.load();
        if (started == 0) {
            continue;
        }
        const unsigned long long stuck_ms = (now_us() - started) / 1000ull;
        if (stuck_ms < 3000) {
            continue;
        }
        // 同一帧只报一次；同一帧持续卡住时最多再补两份（间隔 20s）。
        if (dumps < 3 && (last_report == 0 || now_us() - last_report > 20000000ull)) {
            last_report = now_us();
            write_stall_dump(dir, stuck_ms, g_iter_count.load());
            ++dumps;
        }
    }
    return 0;
}


// --self-test：加载 DLL、创建实例、推进几帧、干净退出。给构建脚本做冒烟用。
//
// 判据是“至少成功推进了一帧”。
// 刻意**不**传 --quit-after：那条路会让引擎自己先收摊，随后再调 dstk_godot_destroy
// 会崩（实测退出码 127）。这里由宿主自己数帧数，到点跳出循环再走正常销毁路径。
bool run_self_test(CreateFn create, BoolFn start, BoolFn iteration, VoidFn destroy,
                   const std::wstring &studio_dir) {
    std::string path_arg = "--path";
    std::string dir_arg = to_utf8(studio_dir);
    std::vector<char *> argv{
        const_cast<char *>("dstk-studio.exe"),
        path_utf8(path_arg), path_utf8(dir_arg),
    };
    void *instance = create(static_cast<int>(argv.size()), argv.data());
    if (!instance) {
        std::fwprintf(stderr, L"[studio] self-test: create failed\n");
        return false;
    }
    if (!start(instance)) {
        destroy(instance);
        std::fwprintf(stderr, L"[studio] self-test: start failed\n");
        return false;
    }
    int frames = 0;
    // 注意语义：dstk_godot_iteration() 返回 **true 表示引擎请求退出**
    // （与 OS_Windows::run 一致），不是"还在跑"。写成 while(iteration()) 会一帧都不跑就退出。
    while (frames < 8 && !iteration(instance)) {
        ++frames;
    }
    // 先打印再销毁：dstk_godot_destroy -> Main::cleanup 之后本进程不再落任何输出
    // （stdout 的缓冲区不会冲刷，实测连静态析构都不跑），所以结果必须在它之前吐出来。
    std::printf("[studio] self-test: %s (%d frames)\n", frames > 0 ? "ok" : "no frames", frames);
    std::fflush(stdout);
    // 仍然要 destroy：否则 Main::cleanup 不跑，退出时会刷一屏 "Unreferenced static string"
    // 和 PagedAllocator 泄漏，看起来像 bug（实际只是没清理）。
    destroy(instance);
    return frames > 0;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
    const std::wstring dir = exe_dir();
    const std::wstring dll_path = dir + L"\\godot\\godot.windows.template_release.x86_64.dll";
    const std::wstring studio_dir = dir + L"\\studio";

    // 必须在引擎创建 Vulkan 实例之前（见函数注释）。
    shield_from_vulkan_overlays();

    // Studio 自己的开关只有 --self-test；其余参数**原样转发**给引擎。
    // 这条通道是无头测试的生命线：tests/run_tests.gd 就是靠
    //   dstk-studio.exe --headless --script res://tests/run_tests.gd
    // 跑起来的。转发必须在 --path 之后，否则引擎解析不到工程路径。
    bool self_test = false;
    std::vector<std::string> passthrough;
    for (int i = 1; i < argc; ++i) {
        if (std::wcscmp(argv[i], L"--self-test") == 0) {
            self_test = true;
            continue;
        }
        passthrough.push_back(to_utf8(argv[i]));
    }

    if (!file_exists(dll_path)) {
        fatal(L"找不到 Godot 运行库：\n" + dll_path +
              L"\n\n请先运行 tools/build_godot.ps1（或在仓库根跑 build.bat）。");
        return 3;
    }
    if (!file_exists(studio_dir + L"\\project.godot")) {
        fatal(L"找不到编辑器工程：\n" + studio_dir +
              L"\n\n请重新构建 —— 工程的 PostBuildEvent 会把 tools/wallpaper-studio/project 拷过来。");
        return 3;
    }

    HMODULE dll = LoadLibraryExW(dll_path.c_str(), nullptr,
                                 LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!dll) {
        fatal(L"加载 Godot 运行库失败（err=" + std::to_wstring(GetLastError()) + L"）：\n" + dll_path);
        return 3;
    }

    auto create = reinterpret_cast<CreateFn>(GetProcAddress(dll, "dstk_godot_create"));
    auto start = reinterpret_cast<BoolFn>(GetProcAddress(dll, "dstk_godot_start"));
    auto iteration = reinterpret_cast<BoolFn>(GetProcAddress(dll, "dstk_godot_iteration"));
    auto destroy = reinterpret_cast<VoidFn>(GetProcAddress(dll, "dstk_godot_destroy"));
    if (!create || !start || !iteration || !destroy) {
        fatal(L"运行库缺少 dstk_* 导出。DLL 与 Studio 不是同一次构建的产物？");
        return 3;
    }

    if (self_test) {
        return run_self_test(create, start, iteration, destroy, studio_dir) ? 0 : 1;
    }

    std::string path_arg = "--path";
    std::string dir_arg = to_utf8(studio_dir);
    std::vector<char *> engine_argv{
        const_cast<char *>("dstk-studio.exe"),
        path_utf8(path_arg), path_utf8(dir_arg),
    };
    for (auto &extra : passthrough) {
        engine_argv.push_back(path_utf8(extra));
    }

    void *instance = create(static_cast<int>(engine_argv.size()), engine_argv.data());
    if (!instance) {
        fatal(L"Godot 实例创建失败。");
        return 1;
    }
    if (!start(instance)) {
        destroy(instance);
        fatal(L"Godot 实例启动失败。");
        return 1;
    }

    // 引擎自己管消息循环与窗口。iteration 返回 true = 引擎请求退出
    // （关窗 / 调用了 quit()）。语义见 godot_instance.h 与 tools/godot-host-test。
    ::CreateThread(nullptr, 0, stall_watch_thread, nullptr, 0, nullptr);
    while (true) {
        g_iter_start_us.store(now_us());
        const bool quit = iteration(instance);
        g_iter_start_us.store(0);
        g_iter_count.fetch_add(1);
        if (quit) {
            break;
        }
    }
    g_watch_stop.store(true);

    // 关窗 = 直接结束进程，**不**调用 dstk_godot_destroy。
    //
    // dstk_godot_destroy -> Main::cleanup() 会递归销毁整棵场景树（编辑器几百个控件
    // 加上内嵌的文件对话框），实测窗口在这段时间里持续 5 秒以上"未响应"，用户看到
    // 的就是"关不掉/卡死"；而且拆到 WinRT/合成层时偶发 fast-fail
    // （RaiseFailFastException，异常码 e0464645，WER 里表现为 CoreMessaging.dll 崩溃）。
    //
    // Studio 是专用宿主进程，退出即结束：窗口由系统关闭，文件都是同步写完的，
    // 没有任何需要在进程内收尾的状态。让系统回收比在进程里慢慢拆更快也更稳。
    // （--self-test 那条路仍然走完整 destroy，那里要的是干净退出。）
    ::ExitProcess(0);
}
