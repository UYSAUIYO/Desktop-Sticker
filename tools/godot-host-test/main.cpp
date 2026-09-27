// Desktop Sticker 场景壁纸 —— M1 宿主测试程序。
//
// 验证目标（对应 .zcode 计划的 M1 验收）：
//   1) 加载我们 fork 出来的 Godot 运行库 DLL（libgodot 接口），全程无外部 Godot 进程；
//   2) 把桌面 WorkerW 层的一个子窗口交给引擎做呈现目标（引擎不建自己的窗口）；
//   3) 迭代渲染一个 Godot 项目，暂停/恢复由宿主控制。
//
// 热键（无需窗口焦点，轮询检测）：F8 暂停 / F9 恢复 / F10 退出。
//
// 用法：
//   dstk-godot-host.exe [--dll <godot dll>] [--project <项目目录> | --pack <pck> | --dswall <壁纸包>] [--top] [--auto-exit <秒>]
//     --dll       默认 ..\godot-src\bin\godot.windows.template_release.x86_64.dll
//     --project   含 project.godot 的目录（默认 ..\godot-wallpaper-template）
//     --pack      Godot PCK 文件（--export-pack 的产物）
//     --dswall    .dswall 壁纸包（ZIP，内含 project.pck；自动解包）
//     --top       不挂 WorkerW，改用顶层无边框窗口（WorkerW 不可用时自动回退）

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// fork 导出的接口
// ---------------------------------------------------------------------------

using SetPresentHwndFn = void (*)(void *);
using GetPresentHwndFn = void *(*)();
using CreateFn = void *(*)(int, char **);
using BoolFn = bool (*)(void *);
using VoidFn = void (*)(void *);

static SetPresentHwndFn pfn_set_present = nullptr;
static CreateFn pfn_create = nullptr;
static BoolFn pfn_start = nullptr;
static BoolFn pfn_iteration = nullptr;
static VoidFn pfn_pause = nullptr;
static VoidFn pfn_resume = nullptr;
static VoidFn pfn_destroy = nullptr;

// ---------------------------------------------------------------------------
// 与壁纸模块一致的 WorkerW 查找（DesktopHost::FindWallpaperWorkerW 的精简版）
// ---------------------------------------------------------------------------

static BOOL CALLBACK find_defview_worker(HWND top, LPARAM param) {
	HWND def_view = FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr);
	if (!def_view) {
		return TRUE;
	}
	// 持有 DefView 的 WorkerW 的下一个兄弟 WorkerW 才是壁纸宿主
	*reinterpret_cast<HWND *>(param) = FindWindowExW(nullptr, top, L"WorkerW", nullptr);
	return FALSE;
}

static HWND find_wallpaper_worker_w() {
	HWND progman = FindWindowW(L"Progman", nullptr);
	if (!progman) {
		return nullptr;
	}
	DWORD_PTR result = 0;
	SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, &result);

	HWND worker = nullptr;
	EnumWindows(find_defview_worker, reinterpret_cast<LPARAM>(&worker));
	return worker;
}

// ---------------------------------------------------------------------------
// 宿主侧小工具
// ---------------------------------------------------------------------------

static double now_ms() {
	static LARGE_INTEGER freq = {};
	if (freq.QuadPart == 0) {
		QueryPerformanceFrequency(&freq);
	}
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return double(t.QuadPart) * 1000.0 / double(freq.QuadPart);
}

static std::string exe_dir() {
	wchar_t path[MAX_PATH]{};
	GetModuleFileNameW(nullptr, path, MAX_PATH);
	std::wstring dir(path);
	size_t slash = dir.find_last_of(L"\\/");
	dir = (slash == std::wstring::npos) ? L"." : dir.substr(0, slash);
	std::string out;
	int n = WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, nullptr, 0, nullptr, nullptr);
	out.resize(n > 0 ? n - 1 : 0);
	if (n > 0) {
		WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, out.data(), n, nullptr, nullptr);
	}
	return out;
}

static std::wstring utf8_to_wide(const std::string &s) {
	int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
	std::wstring out(n > 0 ? n - 1 : 0, L'\0');
	if (n > 0) {
		MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
	}
	return out;
}

static bool key_pressed_once(int vk, bool *was_down) {
	const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
	const bool edge = down && !*was_down;
	*was_down = down;
	return edge;
}
static LRESULT CALLBACK host_wnd_proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
	// 点击穿透：命中测试直接上报 HTTRANSPARENT，不依赖 WS_EX_TRANSPARENT
	//（后者会影响绘制语义，可能让交换链画面出不来）。
	if (msg == WM_NCHITTEST) {
		return HTTRANSPARENT;
	}
	return DefWindowProcW(h, msg, w, l);
}

// 场景壁纸结束后，Explorer 承载壁纸的 WorkerW 空置会露白：
// 把注册表里当前的壁纸路径重设一次（内容不变），强制资源管理器重绘桌面。
static void restore_desktop_wallpaper() {
	wchar_t path[MAX_PATH]{};
	DWORD size = sizeof(path);
	if (RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"WallPaper",
				RRF_RT_REG_SZ, nullptr, path, &size) == ERROR_SUCCESS &&
			path[0] != L'\0') {
		SystemParametersInfoW(0x14 /* SPI_SETDESKWALLPAPER */, 0, path,
				0x3 /* SPIF_UPDATEINIFILE | SPIF_SENDCHANGE */);
		std::printf("[host] desktop wallpaper restored\n");
	}
}

int main(int argc, char **argv) {
	setvbuf(stdout, nullptr, _IONBF, 0); // 无缓冲，崩溃时也能看到日志
	std::string dll_path = exe_dir() + "\\..\\godot-src\\bin\\godot.windows.template_release.x86_64.dll";
	std::string project_path = exe_dir() + "\\..\\godot-wallpaper-template";
	std::string pack_path;
	std::string dswall_path;
	bool allow_top_level = false;
	double auto_exit_sec = 0.0;

	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "--dll") == 0 && i + 1 < argc) {
			dll_path = argv[++i];
		} else if (std::strcmp(argv[i], "--project") == 0 && i + 1 < argc) {
			project_path = argv[++i];
		} else if (std::strcmp(argv[i], "--pack") == 0 && i + 1 < argc) {
			pack_path = argv[++i];
		} else if (std::strcmp(argv[i], "--dswall") == 0 && i + 1 < argc) {
			dswall_path = argv[++i];
		} else if (std::strcmp(argv[i], "--top") == 0) {
			allow_top_level = true;
		} else if (std::strcmp(argv[i], "--auto-exit") == 0 && i + 1 < argc) {
			auto_exit_sec = std::atof(argv[++i]);
		} else {
			std::printf("[host] unknown argument: %s\n", argv[i]);
			return 2;
		}
	}

	// .dswall：ZIP 包，解出 project.pck 后按 --pack 走。
	if (!dswall_path.empty()) {
		if (!pack_path.empty()) {
			std::printf("[host] --pack and --dswall are mutually exclusive\n");
			return 2;
		}
		std::error_code ec;
		const std::filesystem::path out_dir = std::filesystem::temp_directory_path(ec) / "dstk_dswall";
		std::filesystem::remove_all(out_dir, ec);
		std::filesystem::create_directories(out_dir, ec);
		std::wstring cmd = L"tar.exe -xf \"" + utf8_to_wide(dswall_path) + L"\" -C \"" + out_dir.wstring() + L"\"";
		const int tar_rc = _wsystem(cmd.c_str());
		if (tar_rc != 0) {
			std::printf("[host] FATAL: failed to unpack %s (tar rc=%d)\n", dswall_path.c_str(), tar_rc);
			return 1;
		}
		pack_path = (out_dir / "project.pck").string();
		if (!std::filesystem::is_regular_file(pack_path)) {
			std::printf("[host] FATAL: project.pck not found in %s\n", dswall_path.c_str());
			return 1;
		}
		std::printf("[host] dswall unpacked: %s -> %s\n", dswall_path.c_str(), pack_path.c_str());
	}
	const int screen_w = GetSystemMetrics(SM_CXSCREEN);
	const int screen_h = GetSystemMetrics(SM_CYSCREEN);

	// 1) 呈现窗口：优先挂进桌面 WorkerW（壁纸层），失败回退顶层无边框窗口。
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = host_wnd_proc;
	wc.hInstance = GetModuleHandleW(nullptr);
	wc.lpszClassName = L"DstkGodotHostWnd";
	if (!RegisterClassExW(&wc)) {
		std::printf("[host] FATAL: RegisterClassExW failed (err=%lu)\n", GetLastError());
		return 1;
	}

	HWND host_wnd = nullptr;
	HWND worker = find_wallpaper_worker_w();
	// 注意：窗口必须"先隐藏创建，交给引擎注册 AccessKit 适配器，再显示"，
	// 否则 AccessKit 的 Windows 子类化适配器会直接 panic。
	if (worker && !allow_top_level) {
		host_wnd = CreateWindowExW(
				WS_EX_NOACTIVATE,
				L"DstkGodotHostWnd", L"DstkGodotHost", WS_CHILD,
				0, 0, screen_w, screen_h, worker, nullptr, GetModuleHandleW(nullptr), nullptr);
		std::printf("[host] wallpaper WorkerW=%p child=%p (hidden)\n", worker, host_wnd);
	}
	if (!host_wnd) {
		host_wnd = CreateWindowExW(
				WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
				L"DstkGodotHostWnd", L"DstkGodotHost", WS_POPUP,
				0, 0, screen_w, screen_h, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
		std::printf("[host] falling back to top-level window=%p (hidden)\n", host_wnd);
	}
	if (!host_wnd) {
		std::printf("[host] FATAL: could not create present window (err=%lu)\n", GetLastError());
		return 1;
	}

	// 2) 加载 Godot 运行库并解析接口。
	HMODULE dll = LoadLibraryW(utf8_to_wide(dll_path).c_str());
	if (!dll) {
		std::printf("[host] FATAL: LoadLibrary failed (err=%lu): %s\n", GetLastError(), dll_path.c_str());
		return 1;
	}
	pfn_set_present = reinterpret_cast<SetPresentHwndFn>(GetProcAddress(dll, "dstk_embed_set_present_hwnd"));
	pfn_create = reinterpret_cast<CreateFn>(GetProcAddress(dll, "dstk_godot_create"));
	pfn_start = reinterpret_cast<BoolFn>(GetProcAddress(dll, "dstk_godot_start"));
	pfn_iteration = reinterpret_cast<BoolFn>(GetProcAddress(dll, "dstk_godot_iteration"));
	pfn_pause = reinterpret_cast<VoidFn>(GetProcAddress(dll, "dstk_godot_pause"));
	pfn_resume = reinterpret_cast<VoidFn>(GetProcAddress(dll, "dstk_godot_resume"));
	pfn_destroy = reinterpret_cast<VoidFn>(GetProcAddress(dll, "dstk_godot_destroy"));

	bool exports_ok = true;
	struct {
		const char *name;
		void *fn;
		bool required;
	} export_table[] = {
		{ "dstk_embed_set_present_hwnd", reinterpret_cast<void *>(pfn_set_present), true },
		{ "dstk_godot_create", reinterpret_cast<void *>(pfn_create), true },
		{ "dstk_godot_start", reinterpret_cast<void *>(pfn_start), true },
		{ "dstk_godot_iteration", reinterpret_cast<void *>(pfn_iteration), true },
		{ "dstk_godot_pause", reinterpret_cast<void *>(pfn_pause), false },
		{ "dstk_godot_resume", reinterpret_cast<void *>(pfn_resume), false },
		{ "dstk_godot_destroy", reinterpret_cast<void *>(pfn_destroy), true },
	};
	for (const auto &e : export_table) {
		if (!e.fn) {
			std::printf("[host] missing export%s: %s\n", e.required ? "" : " (optional)", e.name);
			if (e.required) {
				exports_ok = false;
			}
		}
	}
	if (!exports_ok) {
		std::printf("[host] FATAL: fork exports missing in %s\n", dll_path.c_str());
		return 1;
	}

	// 3) 把壁纸窗口交给引擎，然后创建并启动实例。
	pfn_set_present(host_wnd);

	char res_arg[32];
	std::snprintf(res_arg, sizeof(res_arg), "%dx%d", screen_w, screen_h);
	char exe_path[MAX_PATH]{};
	GetModuleFileNameA(nullptr, exe_path, MAX_PATH);

	std::vector<char *> engine_argv;
	engine_argv.push_back(exe_path);
	if (!pack_path.empty()) {
		engine_argv.push_back(const_cast<char *>("--main-pack"));
		engine_argv.push_back(pack_path.data());
	} else {
		engine_argv.push_back(const_cast<char *>("--path"));
		engine_argv.push_back(project_path.data());
	}
	engine_argv.push_back(const_cast<char *>("--resolution"));
	engine_argv.push_back(res_arg);
	engine_argv.push_back(const_cast<char *>("--"));
	engine_argv.push_back(const_cast<char *>("--dstk-embedded"));

	const std::string source = pack_path.empty() ? project_path : pack_path;
	std::printf("[host] creating instance (%s=%s, %s)\n",
			pack_path.empty() ? "project" : "pack", source.c_str(), res_arg);
	void *instance = pfn_create(int(engine_argv.size()), engine_argv.data());
	if (!instance) {
		std::printf("[host] FATAL: dstk_godot_create failed\n");
		return 1;
	}
	if (!pfn_start(instance)) {
		std::printf("[host] FATAL: dstk_godot_start failed\n");
		pfn_destroy(instance);
		return 1;
	}
	// 引擎已启动（AccessKit 适配器已注册），现在才显示呈现窗口。
	ShowWindow(host_wnd, SW_SHOWNA);
	std::printf("[host] instance started; hotkeys: F8 pause / F9 resume / F10 quit\n");

	// 4) 宿主驱动主循环：暂停 = 停止迭代（0 CPU/GPU），恢复 = 继续。
	bool paused = false;
	bool f8_down = false, f9_down = false, f10_down = false;
	long long frames = 0;
	double fps_t0 = now_ms();
	const double start_ms = fps_t0;

	for (;;) {
		if (auto_exit_sec > 0.0 && now_ms() - start_ms >= auto_exit_sec * 1000.0) {
			std::printf("[host] auto-exit after %.1fs\n", auto_exit_sec);
			break;
		}
		if (key_pressed_once(VK_F8, &f8_down) && !paused) {
			paused = true;
			if (pfn_pause) {
				pfn_pause(instance);
			}
			std::printf("[host] paused (iteration stopped)\n");
		}
		if (key_pressed_once(VK_F9, &f9_down) && paused) {
			if (pfn_resume) {
				pfn_resume(instance);
			}
			paused = false;
			std::printf("[host] resumed\n");
		}
		if (key_pressed_once(VK_F10, &f10_down)) {
			std::printf("[host] quit requested\n");
			break;
		}

		if (!paused) {
			// GodotInstance::iteration() 返回 true = 引擎请求退出（与 OS_Windows::run 同语义）。
			if (pfn_iteration(instance)) {
				std::printf("[host] engine requested exit\n");
				break;
			}
			++frames;
		} else {
			Sleep(16);
		}

		const double t = now_ms();
		if (t - fps_t0 >= 1000.0) {
			std::printf("[host] fps=%.1f%s\n", frames * 1000.0 / (t - fps_t0), paused ? " (paused)" : "");
			frames = 0;
			fps_t0 = t;
		}
	}

	std::printf("[host] shutting down\n");
	pfn_destroy(instance);
	std::printf("[host] engine destroyed; present window valid=%d\n", IsWindow(host_wnd) ? 1 : 0);
	if (IsWindow(host_wnd)) {
		const BOOL ok = DestroyWindow(host_wnd);
		std::printf("[host] DestroyWindow=%d err=%lu\n", ok ? 1 : 0, ok ? 0UL : GetLastError());
	}
	if (worker) {
		restore_desktop_wallpaper();
	}
	FreeLibrary(dll);
	std::printf("[host] exit\n");
	return 0;
}
