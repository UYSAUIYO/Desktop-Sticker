#include "pch.h"
#include "ProcessSampler.h"

#include "desktopsticker/resmon/CpuMath.h"

#include <set>

namespace desktopsticker::resmon {

namespace {

int64_t filetime_to_100ns(const FILETIME& ft) {
    return (static_cast<int64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

int64_t process_time_100ns(HANDLE h, int64_t* kernel, int64_t* user) {
    FILETIME creation{}, exit{}, k{}, u{};
    if (!GetProcessTimes(h, &creation, &exit, &k, &u)) return -1;
    *kernel = filetime_to_100ns(k);
    *user = filetime_to_100ns(u);
    return 0;
}

// GetThreadDescription 是 Win10 1607+ 才有导出；动态解析以免依赖项目的 WINVER 设置。
std::wstring thread_name(HANDLE h) {
    using Fn = HRESULT(WINAPI*)(HANDLE, PWSTR*);
    static const Fn fn = [] {
        HMODULE k = GetModuleHandleW(L"kernel32.dll");
        return k ? reinterpret_cast<Fn>(GetProcAddress(k, "GetThreadDescription")) : nullptr;
    }();
    if (!fn) return {};

    PWSTR desc = nullptr;
    std::wstring out;
    if (SUCCEEDED(fn(h, &desc)) && desc) {
        out = desc;
        LocalFree(desc);
    }
    return out;
}

} // namespace

int64_t qpc_now_100ns() {
    static const int64_t freq = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f.QuadPart > 0 ? f.QuadPart : 0;
    }();
    if (freq <= 0) return 0;
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    return c.QuadPart * 10000000LL / freq;
}

std::vector<ChildRow> ProcessSampler::SampleChildren() {
    std::vector<ChildRow> rows;
    const DWORD me = GetCurrentProcessId();
    const int64_t wall = qpc_now_100ns();

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return rows;

    std::set<uint32_t> alive;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ParentProcessID != me) continue;

            ChildRow row;
            row.pid = pe.th32ProcessID;
            row.name = pe.szExeFile;
            alive.insert(row.pid);

            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, row.pid);
            if (!h) {
                rows.push_back(std::move(row)); // 拿不到细节也列出，不静默丢弃
                continue;
            }

            int64_t kernel = 0, user = 0;
            if (process_time_100ns(h, &kernel, &user) == 0) {
                CpuSample now{kernel, user, wall};
                const auto it = prevChildren_.find(row.pid);
                if (it != prevChildren_.end()) {
                    row.cpuPercent = cpu_percent(it->second, now).percent;
                }
                prevChildren_[row.pid] = now;
            }

            PROCESS_MEMORY_COUNTERS_EX mc{};
            mc.cb = sizeof(mc);
            if (GetProcessMemoryInfo(h, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&mc), sizeof(mc))) {
                row.workingSetBytes = mc.WorkingSetSize;
            }

            CloseHandle(h);
            rows.push_back(std::move(row));
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    // 回收已退出的子进程，避免 prevChildren_ 无限增长
    for (auto it = prevChildren_.begin(); it != prevChildren_.end();) {
        it = alive.count(it->first) ? std::next(it) : prevChildren_.erase(it);
    }

    std::sort(rows.begin(), rows.end(), [](const ChildRow& a, const ChildRow& b) {
        if (a.cpuPercent != b.cpuPercent) return a.cpuPercent > b.cpuPercent;
        return a.pid < b.pid;
    });
    return rows;
}

CpuSnapshot ProcessSampler::Sample() {
    CpuSnapshot out;
    const DWORD me = GetCurrentProcessId();
    const int64_t wall = qpc_now_100ns();
    out.baseline = (wall == 0);

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;

    std::set<uint32_t> alive;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != me) continue;

            const uint32_t tid = te.th32ThreadID;
            alive.insert(tid);

            HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid);
            if (!h) continue; // 线程已退出

            FILETIME creation{}, exit{}, k{}, u{};
            if (GetThreadTimes(h, &creation, &exit, &k, &u)) {
                CpuSample now{filetime_to_100ns(k), filetime_to_100ns(u), wall};
                ThreadRow row;
                row.tid = tid;
                row.name = thread_name(h);
                row.totalMs = (now.kernel100ns + now.user100ns) / 10000; // 100ns → ms
                const auto it = prevThreads_.find(tid);
                if (it != prevThreads_.end()) {
                    const CpuDelta d = cpu_percent(it->second, now);
                    row.cpuPercent = d.percent;
                    row.baseline = d.baseline;
                    if (d.baseline) out.baseline = true;
                } else {
                    row.baseline = true;
                    out.baseline = true;
                }
                prevThreads_[tid] = now;
                out.threads.push_back(std::move(row));
            }
            CloseHandle(h);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);

    for (auto it = prevThreads_.begin(); it != prevThreads_.end();) {
        it = alive.count(it->first) ? std::next(it) : prevThreads_.erase(it);
    }

    for (const auto& t : out.threads) out.totalPercent += t.cpuPercent;
    if (out.totalPercent > 100.0) out.totalPercent = 100.0;

    std::sort(out.threads.begin(), out.threads.end(), [](const ThreadRow& a, const ThreadRow& b) {
        if (a.cpuPercent != b.cpuPercent) return a.cpuPercent > b.cpuPercent;
        return a.tid < b.tid;
    });

    out.children = SampleChildren();
    return out;
}

} // namespace desktopsticker::resmon
