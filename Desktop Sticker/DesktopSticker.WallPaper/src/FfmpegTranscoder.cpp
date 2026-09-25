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

    const std::wstring encoder = pick_encoder();
    if (encoder.empty()) {
        wp_log("transcode unavailable: no usable H.264 encoder in payload");
        return false;
    }
    const auto args = build_transcode_args(input, output, kind, encoder);
    return run(exePath_, args, timeoutMs);
}

// 固定构建里 libx264 不存在（GPL）。按"系统自带优先、许可最干净"的顺序挑：
// h264_mf（Media Foundation，Windows 必有）→ libopenh264（构建内集成，BSD）→ 硬件编码器。
const std::wstring& FfmpegTranscoder::pick_encoder() {
    if (encoderProbed_) return encoder_;

    static const wchar_t* kCandidates[] = {
        L"h264_mf", L"libopenh264", L"h264_nvenc", L"h264_qsv", L"h264_amf",
    };

    const std::wstring cmd = quote_arg(exePath_) + L" -hide_banner -loglevel error -encoders";
    std::vector<wchar_t> buffer(cmd.begin(), cmd.end());
    buffer.push_back(L'\0');

    std::string output;
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (CreatePipe(&readEnd, &writeEnd, &sa, 0)) {
        SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = writeEnd;
        si.hStdError = writeEnd;
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(exePath_.c_str(), buffer.data(), nullptr, nullptr, TRUE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(writeEnd);
            char chunk[4096];
            DWORD got = 0;
            while (ReadFile(readEnd, chunk, sizeof(chunk), &got, nullptr) && got > 0) {
                output.append(chunk, chunk + got);
            }
            WaitForSingleObject(pi.hProcess, 10000);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        } else {
            CloseHandle(writeEnd);
        }
        CloseHandle(readEnd);
    }

    for (const wchar_t* candidate : kCandidates) {
        const std::string narrow(candidate, candidate + wcslen(candidate));
        if (output.find(narrow) != std::string::npos) {
            encoder_ = candidate;
            wp_log("transcode encoder selected: " + narrow);
            break;
        }
    }
    if (encoder_.empty()) {
        wp_log("no candidate H.264 encoder found in payload");
    }
    encoderProbed_ = true;
    return encoder_;
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
