#include "pch.h"
#include "FfmpegTranscoder.h"

#include "Log.h"
#include "Utf8.h"
#include "desktopsticker/wallpaper/FfmpegCommand.h"

namespace fs = std::filesystem;

namespace desktopsticker::wallpaper {

namespace {

// 按 Windows 命令行规则加引号：只保留必要的引号包裹，绝不经由 shell 解释
std::wstring quote_arg(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;

    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : a) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
            backslashes = 0;
            continue;
        }
        out.append(backslashes, L'\\');
        backslashes = 0;
        out.push_back(c);
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

std::wstring build_command_line(const std::wstring& exe, const std::vector<std::wstring>& args) {
    std::wstring line = quote_arg(exe);
    for (const auto& a : args) {
        line.push_back(L' ');
        line += quote_arg(a);
    }
    return line;
}

} // namespace

FfmpegTranscoder::FfmpegTranscoder(std::wstring ffmpegDir) {
    exePath_ = (fs::path(std::move(ffmpegDir)) / L"ffmpeg.exe").wstring();
}

bool FfmpegTranscoder::Available() const {
    std::error_code ec;
    return fs::is_regular_file(exePath_, ec);
}

bool FfmpegTranscoder::RunTranscode(const std::wstring& input,
                                    const std::wstring& output,
                                    VariantKind kind,
                                    unsigned timeoutMs) {
    if (kind == VariantKind::Original) return false;

    std::error_code ec;
    // 硬约束：输出必须与源不同，且必须落在派生目录里
    if (fs::weakly_canonical(input, ec) == fs::weakly_canonical(output, ec)) {
        wp_log("transcode refused: output equals source");
        return false;
    }
    if (!fs::is_regular_file(input, ec)) {
        wp_log("transcode refused: input missing " + to_utf8(input));
        return false;
    }
    fs::create_directories(fs::path(output).parent_path(), ec);

    const auto args = build_transcode_args(input, output, kind);
    return run(exePath_, args, timeoutMs);
}

bool FfmpegTranscoder::RunThumbnail(const std::wstring& input,
                                    const std::wstring& outputPng,
                                    int scaleWidth,
                                    unsigned timeoutMs) {
    std::error_code ec;
    if (!fs::is_regular_file(input, ec)) return false;
    fs::create_directories(fs::path(outputPng).parent_path(), ec);

    std::vector<std::wstring> args = {
        L"-hide_banner", L"-loglevel", L"error",
        L"-nostdin",
        L"-y",
        L"-i", input,
    };
    if (scaleWidth > 0) {
        args.push_back(L"-vf");
        args.push_back(L"scale=" + std::to_wstring(scaleWidth) + L":-2");
    }
    args.push_back(L"-frames:v");
    args.push_back(L"1");
    args.push_back(outputPng);

    return run(exePath_, args, timeoutMs);
}

bool FfmpegTranscoder::run(const std::wstring& exe,
                          const std::vector<std::wstring>& args,
                          unsigned timeoutMs) {
    if (!Available()) {
        wp_log("ffmpeg.exe unavailable: " + to_utf8(exePath_));
        return false;
    }

    std::lock_guard<std::mutex> lock(queueMutex_); // 串行队列

    std::wstring commandLine = build_command_line(exe, args);
    std::vector<wchar_t> mutableCmd(commandLine.begin(), commandLine.end());
    mutableCmd.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    const std::wstring workDir = fs::path(exe).parent_path().wstring();
    const BOOL created = CreateProcessW(
        exe.c_str(),
        mutableCmd.data(),
        nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        workDir.empty() ? nullptr : workDir.c_str(),
        &si, &pi);
    if (!created) {
        wp_log("CreateProcessW failed (" + std::to_string(GetLastError()) + ") for ffmpeg");
        return false;
    }

    const DWORD wait = WaitForSingleObject(pi.hProcess, timeoutMs);
    if (wait == WAIT_TIMEOUT) {
        wp_log("ffmpeg timed out; terminating");
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (wait == WAIT_TIMEOUT || exitCode != 0) {
        wp_log("ffmpeg failed, exit=" + std::to_string(exitCode));
        return false;
    }
    return true;
}

} // namespace desktopsticker::wallpaper
