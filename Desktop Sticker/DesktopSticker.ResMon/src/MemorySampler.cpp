#include "pch.h"
#include "MemorySampler.h"

#include "desktopsticker/resmon/Classify.h"

namespace desktopsticker::resmon {

MemorySnapshot MemorySampler::Sample(const std::wstring& exeDir) {
    MemorySnapshot out;

    PROCESS_MEMORY_COUNTERS_EX mc{};
    mc.cb = sizeof(mc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&mc),
                             sizeof(mc))) {
        out.workingSetBytes = mc.WorkingSetSize;
        out.privateBytes = mc.PrivateUsage;
        out.peakWorkingSetBytes = mc.PeakWorkingSetSize;
    }

    HANDLE proc = GetCurrentProcess();
    DWORD needed = 0;
    if (EnumProcessModules(proc, nullptr, 0, &needed) && needed > 0) {
        std::vector<HMODULE> modules(needed / sizeof(HMODULE));
        DWORD got = 0;
        if (EnumProcessModules(proc, modules.data(),
                               static_cast<DWORD>(modules.size() * sizeof(HMODULE)), &got)) {
            const size_t count = got / sizeof(HMODULE);
            out.modules.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                MODULEINFO mi{};
                if (!GetModuleInformation(proc, modules[i], &mi, sizeof(mi))) continue;

                // 取完整路径做归属判定：仅靠基名无法区分系统库与随包负载
                wchar_t full[MAX_PATH]{};
                if (GetModuleFileNameW(modules[i], full, MAX_PATH) == 0) continue;
                if (!is_own_module(full, exeDir)) continue;

                ModuleRow row;
                row.name = detail::name_of(full);
                row.imageBytes = mi.SizeOfImage;
                out.modules.push_back(std::move(row));
            }
        }
    }

    std::sort(out.modules.begin(), out.modules.end(),
              [](const ModuleRow& a, const ModuleRow& b) {
                  if (a.imageBytes != b.imageBytes) return a.imageBytes > b.imageBytes;
                  return a.name < b.name;
              });

    out.children = children_.SampleChildren();
    return out;
}

} // namespace desktopsticker::resmon
