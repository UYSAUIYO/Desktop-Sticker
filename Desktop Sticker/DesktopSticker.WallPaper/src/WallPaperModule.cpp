#include "pch.h"

#include "desktopsticker/IWallPaperModule.h"

#include "AudioEngine.h"
#include "DriveInventory.h"
#include "FfmpegApi.h"
#include "FfmpegTranscoder.h"
#include "FrameSchedulerLoop.h"
#include "FullscreenDetector.h"
#include "Log.h"
#include "MediaLibrary.h"
#include "Utf8.h"
#include "VideoSource.h"
#include "WallPaperStore.h"
#include "WallpaperBackend.h"

#include <shlobj.h>
#include <shobjidl.h>

#include "desktopsticker/wallpaper/FfmpegCommand.h"
#include "desktopsticker/wallpaper/PausePolicy.h"
#include "desktopsticker/wallpaper/VariantPolicy.h"

#include <atomic>

namespace desktopsticker {
namespace {

using namespace desktopsticker::wallpaper;

constexpr int kVariantRevision = 1;

std::wstring app_data_dir() {
    PWSTR appData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) return {};
    std::filesystem::path root(appData);
    CoTaskMemFree(appData);
    root /= L"DesktopSticker";
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    return root.wstring();
}

std::wstring exe_dir() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path().wstring();
}

// HBITMAP → HICON：CreateIconIndirect 需要一个掩码位图，单色即可
HICON hbitmap_to_hicon(HBITMAP color, int size) {
    if (!color) return nullptr;
    HBITMAP scaled = static_cast<HBITMAP>(CopyImage(color, IMAGE_BITMAP, size, size, 0));
    if (!scaled) return nullptr;

    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmColor = scaled;
    ii.hbmMask = mask;
    HICON icon = CreateIconIndirect(&ii);

    DeleteObject(scaled);
    if (mask) DeleteObject(mask);
    return icon;
}

class WallPaperModuleImpl final : public IWallPaperModule {
public:
    WallPaperModuleImpl();
    ~WallPaperModuleImpl() override;

    bool Init(const WallPaperEvents& events) override;
    bool Start() override;
    void Stop() override;
    void Shutdown() override;

    std::vector<WallPaperItem> ListItems() override;
    bool Import(const std::wstring& srcPath, std::wstring& outId) override;
    bool Rename(const std::wstring& id, const std::wstring& name) override;
    bool Remove(const std::wstring& id) override;
    HICON GetThumbnail(const std::wstring& id, int size) override;

    WallPaperSettings GetSettings() override;
    bool SetSettings(const WallPaperSettings& settings) override;
    bool RegenerateVariant(const std::wstring& id, VariantKind kind) override;
    bool ChangeLibraryRoot(const std::wstring& newRoot) override;
    void SetUserPaused(bool paused) override;
    bool IsUserPaused() override;
    bool Available() override;

private:
    bool ensure_library_root();
    void bind_storage_record(const std::wstring& root);
    bool request_play(const std::wstring& id);
    void stop_playback();
    void start_monitor();
    void stop_monitor();
    void monitor_main();
    void queue_prepare_artifacts(const std::wstring& id, VariantKind kind = VariantKind::Balanced);
    std::wstring ffmpeg_dir() const;
    void notify_playback();

    WallPaperEvents events_;
    WallPaperStore store_;
    std::unique_ptr<MediaLibrary> library_;
    std::unique_ptr<FfmpegTranscoder> transcoder_;
    FrameSchedulerLoop loop_;
    AudioEngine audio_;

    PersistedState state_;
    std::wstring configDir_;
    bool available_ = false;
    bool initialized_ = false;
    std::atomic<bool> userPaused_{false};
    PlaybackState pausing_state_last_ = PlaybackState::Playing;
    std::mutex libraryMutex_; // 后台生成缩略图/副本时会改写 library.json

    // 缩略图缓存：模块持有，调用方不得 DestroyIcon
    std::mutex thumbMutex_;
    std::map<std::wstring, HICON> thumbs_;

    std::thread monitorThread_;
    std::atomic<bool> monitorQuit_{false};
    std::thread workerThread_;
    std::atomic<bool> workerQuit_{false};
};

WallPaperModuleImpl::WallPaperModuleImpl()
    : store_(app_data_dir().empty() ? std::wstring(L".") : app_data_dir()) {}

WallPaperModuleImpl::~WallPaperModuleImpl() {
    Shutdown();
}

std::wstring WallPaperModuleImpl::ffmpeg_dir() const {
    return (std::filesystem::path(exe_dir()) / L"ffmpeg").wstring();
}

bool WallPaperModuleImpl::Init(const WallPaperEvents& events) {
    if (initialized_) return true;
    events_ = events;
    configDir_ = store_.ConfigDir();
    state_ = store_.LoadState();

    transcoder_ = std::make_unique<FfmpegTranscoder>(ffmpeg_dir());

    // FFmpeg 共享库是兜底路径，加载失败不影响 MF 主路径
    FfmpegApi::Get().Load(ffmpeg_dir());

    initialized_ = true;
    return true;
}

bool WallPaperModuleImpl::ensure_library_root() {
    // 已绑定：每次启动都要校验盘符没被复用成别的卷
    if (state_.storage.bound && !state_.storage.root.empty()) {
        VolumeIdentity expected{};
        expected.serial = state_.storage.volumeSerial;
        expected.rootFileId = state_.storage.rootFileId;

        if (verify_volume_identity(state_.storage.root, expected)) {
            return true;
        }
        wp_log("wallpaper library volume identity mismatch; degrading. root=" +
               to_utf8(state_.storage.root));
        return false;
    }

    // 首次启用：选剩余空间最大的固定盘
    const auto root = resolve_library_root();
    if (!root) return false;

    bind_storage_record(*root);
    state_.settings.libraryRoot = *root;
    store_.SaveState(state_);
    return true;
}

void WallPaperModuleImpl::bind_storage_record(const std::wstring& root) {
    state_.storage.root = root;
    state_.storage.bound = false;
    state_.storage.volumeSerial = 0;
    state_.storage.rootFileId = 0;

    VolumeIdentity id{};
    if (probe_volume_identity(root, id)) {
        state_.storage.volumeSerial = id.serial;
        state_.storage.rootFileId = id.rootFileId;
        state_.storage.bound = true;
    } else {
        wp_log("probe volume identity failed for " + to_utf8(root));
    }
}

bool WallPaperModuleImpl::Start() {
    if (!initialized_) return false;
    if (loop_.Running()) return true;

    if (!ensure_library_root()) {
        wp_log("wallpaper unavailable: library root not usable");
        available_ = false;
        return false;
    }

    library_ = std::make_unique<MediaLibrary>(state_.storage.root, store_);
    if (!video_subsystem_start()) {
        wp_log("video subsystem start failed");
        available_ = false;
        return false;
    }

    available_ = true;
    start_monitor();

    if (state_.settings.enabled && !state_.settings.activeId.empty()) {
        request_play(state_.settings.activeId);
    }
    wp_log("wallpaper module started; root=" + to_utf8(state_.storage.root));
    return true;
}

void WallPaperModuleImpl::Stop() {
    stop_monitor();
    stop_playback();
    workerQuit_.store(true);
    if (workerThread_.joinable()) workerThread_.join();

    {
        std::lock_guard<std::mutex> lock(thumbMutex_);
        for (auto& [id, icon] : thumbs_) {
            if (icon) DestroyIcon(icon);
        }
        thumbs_.clear();
    }
    video_subsystem_stop();
    library_.reset();
    available_ = false;
}

void WallPaperModuleImpl::Shutdown() {
    if (!initialized_) return;
    Stop();
    store_.SaveState(state_);
    initialized_ = false;
}

std::vector<WallPaperItem> WallPaperModuleImpl::ListItems() {
    if (!library_) return {};
    return library_->List();
}

bool WallPaperModuleImpl::Import(const std::wstring& srcPath, std::wstring& outId) {
    if (!library_) return false;
    if (!library_->Import(srcPath, outId)) return false;

    queue_prepare_artifacts(outId); // 缩略图与性能副本后台生成，不阻塞调用方
    if (events_.libraryChanged) events_.libraryChanged();

    // 首次导入即启用，省去用户再点一次开关
    if (state_.settings.activeId.empty()) {
        state_.settings.enabled = true;
        state_.settings.activeId = outId;
        store_.SaveState(state_);
        request_play(outId);
    }
    return true;
}

bool WallPaperModuleImpl::Rename(const std::wstring& id, const std::wstring& name) {
    if (!library_ || !library_->Rename(id, name)) return false;
    if (events_.libraryChanged) events_.libraryChanged();
    return true;
}

bool WallPaperModuleImpl::Remove(const std::wstring& id) {
    if (!library_ || !library_->Remove(id)) return false;

    if (state_.settings.activeId == id) {
        state_.settings.activeId.clear();
        store_.SaveState(state_);
        stop_playback();
    }
    if (events_.libraryChanged) events_.libraryChanged();
    return true;
}

HICON WallPaperModuleImpl::GetThumbnail(const std::wstring& id, int size) {
    if (!library_) return nullptr;

    const std::wstring key = id + L"@" + std::to_wstring(size);
    {
        std::lock_guard<std::mutex> lock(thumbMutex_);
        const auto it = thumbs_.find(key);
        if (it != thumbs_.end()) return it->second;
    }

    // 优先用生成好的 poster.png，缺失时退回 Shell 缩略图
    std::wstring picture = library_->PosterPath(id);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(picture, ec)) {
        picture.clear();
        for (const auto& item : library_->List()) {
            if (item.id == id) { picture = library_->SourcePath(item); break; }
        }
    }
    if (picture.empty()) return nullptr;

    HICON icon = nullptr;
    Microsoft::WRL::ComPtr<IShellItemImageFactory> factory;
    if (SUCCEEDED(SHCreateItemFromParsingName(picture.c_str(), nullptr,
                                              IID_PPV_ARGS(&factory)))) {
        HBITMAP bmp = nullptr;
        const SIZE want{ size, size };
        if (SUCCEEDED(factory->GetImage(want, SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK, &bmp))) {
            icon = hbitmap_to_hicon(bmp, size);
            DeleteObject(bmp);
        }
    }

    std::lock_guard<std::mutex> lock(thumbMutex_);
    thumbs_[key] = icon; // 失败也缓存 nullptr，避免反复尝试
    return icon;
}

WallPaperSettings WallPaperModuleImpl::GetSettings() {
    WallPaperSettings out = state_.settings;
    out.libraryRoot = state_.storage.root;
    return out;
}

bool WallPaperModuleImpl::SetSettings(const WallPaperSettings& settings) {
    const bool wasEnabled = state_.settings.enabled;
    const std::wstring previousActive = state_.settings.activeId;
    const double previousSpeed = state_.settings.speed;
    const bool previousAudio = state_.settings.audioEnabled;
    const float previousVolume = state_.settings.audioVolume;

    state_.settings = settings;
    store_.SaveState(state_);

    if (!settings.enabled) {
        stop_playback();
        notify_playback();
        return true;
    }

    // 音频与速度是"热"设置：不重开解码，直接生效
    audio_.SetMuted(!settings.audioEnabled);
    audio_.SetVolume(settings.audioVolume);
    if (settings.speed != previousSpeed) {
        audio_.SetSpeed(settings.speed);
        loop_.SetSpeed(settings.speed);
    }
    (void)previousAudio;
    (void)previousVolume;

    // 换壁纸或换档位才需要重开后端
    if (!wasEnabled || previousActive != settings.activeId) {
        request_play(settings.activeId);
    }
    notify_playback();
    return true;
}

bool WallPaperModuleImpl::RegenerateVariant(const std::wstring& id, VariantKind kind) {
    if (!library_ || kind == VariantKind::Original) return false;
    queue_prepare_artifacts(id, kind);
    return true;
}

bool WallPaperModuleImpl::ChangeLibraryRoot(const std::wstring& newRoot) {
    if (!library_) return false;

    std::error_code ec;
    const std::filesystem::path dst(newRoot);
    std::filesystem::create_directories(dst, ec);
    if (ec) {
        wp_log("change root failed: " + ec.message());
        return false;
    }

    const std::filesystem::path src(state_.storage.root);
    // 简单复制：库不大，逐文件拷贝并校验大小；旧位置保留不删（可回滚）
    for (const auto& entry : std::filesystem::recursive_directory_iterator(src, ec)) {
        if (!entry.is_regular_file()) continue;
        const auto rel = std::filesystem::relative(entry.path(), src, ec);
        if (ec) continue;
        const auto target = dst / rel;
        std::filesystem::create_directories(target.parent_path(), ec);
        std::filesystem::copy_file(entry.path(), target,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            wp_log("change root copy failed: " + ec.message());
            return false;
        }
        if (std::filesystem::file_size(entry.path(), ec) != std::filesystem::file_size(target, ec)) {
            wp_log("change root size mismatch for " + to_utf8(rel.wstring()));
            return false;
        }
    }

    stop_playback();
    state_.storage.root = newRoot;
    bind_storage_record(newRoot);
    state_.settings.libraryRoot = newRoot;
    store_.SaveState(state_);

    library_ = std::make_unique<MediaLibrary>(newRoot, store_);
    if (state_.settings.enabled && !state_.settings.activeId.empty()) {
        request_play(state_.settings.activeId);
    }
    wp_log("wallpaper library root changed to " + to_utf8(newRoot) + " (old copy kept)");
    if (events_.libraryChanged) events_.libraryChanged();
    return true;
}

void WallPaperModuleImpl::SetUserPaused(bool paused) {
    userPaused_.store(paused);
}

bool WallPaperModuleImpl::IsUserPaused() {
    return userPaused_.load();
}

bool WallPaperModuleImpl::Available() {
    return available_;
}

bool WallPaperModuleImpl::request_play(const std::wstring& id) {
    if (!library_ || id.empty()) return false;

    std::wstring path;
    BackendKind kind = BackendKind::Video;
    for (const auto& item : library_->List()) {
        if (item.id != id) continue;
        kind = item.kind;

        if (kind != BackendKind::Video) {
            // 目录型后端：源就是条目里的对应子目录
            const std::filesystem::path dir(library_->ItemDir(item.id));
            const wchar_t* sub = (kind == BackendKind::ImageSequence) ? L"frames"
                                 : (kind == BackendKind::Web)           ? L"web"
                                                                        : L"shader";
            path = (dir / sub).wstring();
            break;
        }

        // 档位只对视频有意义：首选副本不存在就回落原画
        VariantAvailability avail;
        avail.hasBalanced = item.hasBalanced &&
            std::filesystem::is_regular_file(library_->VariantPath(id, VariantKind::Balanced,
                                                                   kVariantRevision));
        avail.hasPowerSaver = item.hasPowerSaver &&
            std::filesystem::is_regular_file(library_->VariantPath(id, VariantKind::PowerSaver,
                                                                   kVariantRevision));
        const VariantKind effective =
            resolve_effective_variant(state_.settings.preferred, avail);

        path = (effective == VariantKind::Original)
                   ? library_->SourcePath(item)
                   : library_->VariantPath(id, effective, kVariantRevision);
        break;
    }
    if (path.empty()) return false;

    if (!backend_available(kind)) {
        wp_log("requested backend is not available in this build; keeping current picture");
        return false;
    }

    // 引擎与路径只需在首次启动前设置一次
    if (!loop_.Running()) {
        loop_.SetAudioEngine(&audio_);
        loop_.SetPaths(exe_dir(), state_.storage.root);
        if (!loop_.Start()) {
            wp_log("playback start failed: render thread init error");
            available_ = false;
            return false;
        }
    }

    BackendRequest request;
    request.kind = kind;
    request.sourcePath = path;
    request.speed = state_.settings.speed;
    loop_.SetBackendRequest(request);   // 在渲染线程上打开（解码器不跨线程）
    loop_.SetPaused(false);

    // 音频设置随时可能变，这里同步一次
    audio_.SetMuted(!state_.settings.audioEnabled);
    audio_.SetVolume(state_.settings.audioVolume);
    audio_.SetSpeed(state_.settings.speed);

    notify_playback();
    return true;
}

void WallPaperModuleImpl::stop_playback() {
    if (!loop_.Running()) return;
    loop_.ClearBackend();
    loop_.SetPaused(true);
    loop_.Stop();
}

void WallPaperModuleImpl::queue_prepare_artifacts(const std::wstring& id, VariantKind kind) {
    if (!library_ || !transcoder_) return;

    std::wstring source;
    for (const auto& item : library_->List()) {
        if (item.id == id) { source = library_->SourcePath(item); break; }
    }
    if (source.empty()) return;

    const std::wstring poster = library_->PosterPath(id);
    const std::wstring variant = library_->VariantPath(id, kind, kVariantRevision);

    // 单条后台工作线程串行处理；转码本身在 FfmpegTranscoder 内也已串行
    if (workerThread_.joinable()) {
        workerQuit_.store(true);
        workerThread_.join();
        workerQuit_.store(false);
    }
    workerThread_ = std::thread([this, source, poster, variant, kind, id]() {
        SetThreadDescription(GetCurrentThread(), L"壁纸转码工作");
        if (!transcoder_) return;

        if (transcoder_->RunThumbnail(source, poster)) {
            std::lock_guard<std::mutex> lock(libraryMutex_);
            library_->Update(id, [](WallPaperItem& it) { it.hasPoster = true; });
        }
        if (!workerQuit_.load() && transcoder_->RunTranscode(source, variant, kind)) {
            std::lock_guard<std::mutex> lock(libraryMutex_);
            library_->Update(id, [&](WallPaperItem& it) {
                if (kind == VariantKind::PowerSaver) it.hasPowerSaver = true;
                else it.hasBalanced = true;
            });
        }
        if (events_.libraryChanged) events_.libraryChanged();
    });
}

void WallPaperModuleImpl::notify_playback() {
    if (events_.playbackStateChanged) events_.playbackStateChanged();
}

void WallPaperModuleImpl::start_monitor() {
    if (monitorThread_.joinable()) return;
    monitorQuit_.store(false);
    monitorThread_ = std::thread([this]() {
        SetThreadDescription(GetCurrentThread(), L"壁纸暂停监控");
        monitor_main();
    });
}

void WallPaperModuleImpl::stop_monitor() {
    if (!monitorThread_.joinable()) return;
    monitorQuit_.store(true);
    monitorThread_.join();
}

void WallPaperModuleImpl::monitor_main() {
    while (!monitorQuit_.load()) {
        for (int i = 0; i < 10 && !monitorQuit_.load(); ++i) {
            Sleep(100); // 合计约 1s 的兜底轮询
        }
        if (monitorQuit_.load()) break;

        const auto settings = GetSettings();
        const HWND self = loop_.Window();
        const SystemFacts facts = FullscreenDetector::Collect(self);

        PauseInputs in;
        in.sessionLocked = facts.sessionLocked && settings.pauseOnLock;
        in.displayOff = facts.displayOff && settings.pauseOnLock;
        in.userPaused = userPaused_.load();
        in.fullscreenCovered = facts.fullscreenCovered && settings.pauseOnFullscreen;

        const PauseDecision decision = reduce_pause_policy(in);
        const bool shouldPause = (decision.state == PlaybackState::Paused);
        if (shouldPause != loop_.Paused()) {
            loop_.SetPaused(shouldPause);
            wp_log(std::string("playback ") + (shouldPause ? "paused" : "resumed"));
        }
        if (decision.state != pausing_state_last_) {
            pausing_state_last_ = decision.state;
            notify_playback();
        }
    }
}

} // namespace
} // namespace desktopsticker

// ---- 导出工厂 ----

extern "C" DESKTOPSTICKER_WALLPAPER_API desktopsticker::IWallPaperModule* CreateWallPaperModule() {
    try {
        return new desktopsticker::WallPaperModuleImpl();
    } catch (...) {
        return nullptr;
    }
}

extern "C" DESKTOPSTICKER_WALLPAPER_API void DestroyWallPaperModule(
    desktopsticker::IWallPaperModule* module) {
    delete module;
}
