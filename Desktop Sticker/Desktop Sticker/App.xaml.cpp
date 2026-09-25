#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

#include "AppLog.h"
#include "WindowChrome.h"

#include <winrt/Microsoft.UI.Dispatching.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Desktop_Sticker::implementation
{
    App::App()
    {
#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& e)
        {
            if (IsDebuggerPresent())
            {
                auto errorMessage = e.Message();
                __debugbreak();
            }
        });
#endif
        // 跟随系统深浅色（解包应用不会自动应用）。只能在 App 构造阶段设置，
        // 晚了会抛异常；标题栏/控件颜色都依赖它保持一致
        if (desktopsticker::app::IsSystemDarkMode()) {
            try {
                RequestedTheme(ApplicationTheme::Dark);
            } catch (...) {
            }
        }
    }

    void App::OnLaunched([[maybe_unused]] LaunchActivatedEventArgs const& e)
    {
        // 单实例保护：多个实例同时移动桌面图标会导致图标来回闪烁
        static HANDLE s_singleInstanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\DesktopSticker.SingleInstance");
        if (!s_singleInstanceMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
            Application::Current().Exit();
            return;
        }

        // 主窗口只作为托盘宿主，创建后立即隐藏；
        // 托盘先就绪，功能初始化失败才有地方向用户提示（气泡），不允许静默失败。
        // 注意顺序：AttachHost 负责填充 m_hwnd 并加托盘图标，必须先于 Activate/隐藏调用
        auto mainWindow = make<MainWindow>();
        auto impl = winrt::get_self<implementation::MainWindow>(mainWindow);
        window = mainWindow;

        m_host = std::make_unique<desktopsticker::app::Host>();
        impl->AttachHost(m_host.get(), nullptr);

        window.Activate();
        ShowWindow(impl->Hwnd(), SW_HIDE);
        m_dispatcher = window.DispatcherQueue();

        // 给 UI 线程一个可读名字，资源管理器的 CPU 页据此显示线程归属
        SetThreadDescription(GetCurrentThread(), L"UI / 分区渲染");

        if (!m_host->LoadFeatures()) {
            impl->ShowTrayWarning(L"功能模块加载失败（缺 DesktopSticker.Features.dll 或初始化异常），详见 %APPDATA%\\DesktopSticker\\debug.log");
        } else {
            m_host->SetHotkeyCallback([this]() {
                if (m_dispatcher) {
                    m_dispatcher.TryEnqueue([this]() {
                        // 面板打开（含焦点在面板上）时双击空格不再关闭——避免搜索框输入
                        // 空格/手抖多敲一下导致面板闪现即关；关闭只走 Esc
                        if (!m_launcher->Visible()) {
                            m_launcher->Show();
                        }
                    });
                }
            });
            if (!m_host->Start()) {
                impl->ShowTrayWarning(L"桌面分区初始化失败（桌面嵌入异常），磁贴不可用，详见 %APPDATA%\\DesktopSticker\\debug.log");
            }
        }

        // 动态桌面壁纸是可选组件：加载或初始化失败一律降级为"壁纸不可用"，
        // 不影响分区、搜索启动器与桌面时钟，因此这里只记日志不弹提示。
        m_host->SetWallPaperEvents(
            [this]() {
                if (m_dispatcher) m_dispatcher.TryEnqueue([this]() {
                    if (m_settings) m_settings->RefreshWallPaper();
                });
            },
            [this]() {
                if (m_dispatcher) m_dispatcher.TryEnqueue([this]() {
                    if (m_settings) m_settings->RefreshWallPaper();
                });
            });
        m_host->LoadWallPaper();

        // 资源管理器同为可选组件：失败只让托盘菜单项置灰，不弹提示、不阻断启动。
        m_host->LoadResMon();

        m_launcher = std::make_unique<desktopsticker::app::LauncherController>(m_host.get());
        m_settings = std::make_unique<desktopsticker::app::SettingsController>(m_host.get());
        impl->AttachSettings(m_settings.get());

        // 命令行 --settings：启动即打开设置窗口（便于测试与快捷入口）。
        // 解包 WinUI3 的 e.Arguments() 恒为空，必须读完整命令行
        const std::wstring args(GetCommandLineW());
        {
            char buf[512]{};
            snprintf(buf, sizeof(buf), "launch args: %ls", args.c_str());
            desktopsticker::app::AppLog("app", buf);
        }
        if (args.find(L"--settings") != std::wstring::npos) {
            m_settings->Show();
        }
        // 命令行 --resmon：启动即打开资源管理器（与 --settings 对称的快捷入口）
        if (args.find(L"--resmon") != std::wstring::npos && m_host->ResMon() &&
            m_host->ResMon()->Available()) {
            m_host->ResMon()->Show();
        }
    }
}
