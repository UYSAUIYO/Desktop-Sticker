#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj.h>

// 图形与媒体：本项目独有的 D3D/DComp/MF 栈（Features DLL 只有 D2D/ULW，两者不混用）
#include <d3d11.h>
#include <d3d11_3.h>       // ID3D11Device3 + SRV desc1：NV12 的 PlaneSlice 只在这里
#include <d3d11_4.h>       // ID3D11Multithread：与 MF 硬解共享 immediate context 的设备级锁
#include <d3dcompiler.h>   // 运行时编译 NV12 上屏的两段 HLSL
#include <dxgi1_2.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <dcomp.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>   // 系统静音状态（IAudioEndpointVolume::GetMute）
#include <mmreg.h>
#include <audioclient.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <wrl/implements.h>
#include <wrl/event.h>
#include "WebView2.h"   // ③ Web 壁纸后端（自呈现型）

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
