#include "pch.h"
#include "desktopsticker/IconClassifier.h"

#include <filesystem>

#include "desktopsticker/StringUtil.h"

namespace fs = std::filesystem;

namespace desktopsticker {

namespace {

bool ContainsAny(const std::wstring& lowerName, const std::vector<std::wstring>& keys) {
    for (const auto& key : keys) {
        if (lowerName.find(key) != std::wstring::npos) return true;
    }
    return false;
}

// 目录名自带语义的少量归入内容分区，其余为文件夹
std::wstring ClassifyDirectory(const std::wstring& nameLower) {
    if (nameLower.find(L"游戏") != std::wstring::npos) return L"游戏";
    if (nameLower.find(L"电影") != std::wstring::npos || nameLower.find(L"视频") != std::wstring::npos)
        return L"影音娱乐";
    return L"文件夹";
}

} // namespace

const std::vector<IconClassifier::CategoryRules>& IconClassifier::AppRules() {
    static const std::vector<CategoryRules> rules = {
        // 个别与通用关键字冲突的先判（FL Studio 含 "studio" 却是音乐软件）
        {L"影音娱乐", {L"fl studio"}},

        {L"开发工具", {L"visual studio", L"code", L"stm32", L"keil", L"git",
                       L"python", L"node", L"ide", L"idea", L"arduino", L"kicad",
                       L"cube", L"docker", L"terminal", L"qt", L"cmake",
                       L"开发", L"编程", L"compiler", L"ida", L"multisim", L"fusion",
                       L"android", L"studio", L"vmware", L"virtualbox", L"eda",
                       L"esp", L"idf", L"smartrf", L"matlab", L"labview", L"altium",
                       L"unity", L"unreal", L"jdk", L"java", L"pycharm", L"clion",
                       L"rider", L"postman", L"navicat", L"redis", L"nginx",
                       L"sdk", L"ndk", L"adb", L"烧录", L"调试", L"仿真",
                       L"单片机", L"嵌入式", L"串口", L"wireshark", L"proteus",
                       L"github", L"gitlab", L"putty", L"xshell", L"gradle", L"maven"}},

        {L"浏览器", {L"chrome", L"edge", L"firefox", L"浏览器", L"brave",
                     L"360安全浏览器", L"qq浏览器", L"internet explorer",
                     L"opera", L"vivaldi"}},

        {L"办公软件", {L"word", L"excel", L"powerpoint", L"office", L"wps", L"pdf",
                       L"onenote", L"outlook", L"办公", L"officeai", L"wps office",
                       L"xls", L"doc", L"xmind", L"思维导图", L"mindmaster",
                       L"visio", L"foxit", L"福昕", L"typora", L"markdown",
                       L"notion", L"obsidian", L"zotero", L"endnote", L"calibre",
                       L"sumatra", L"稻壳", L"ocr", L"文字识别"}},

        {L"影音娱乐", {L"potplayer", L"vlc", L"music", L"video", L"播放", L"音乐",
                       L"网易云", L"qq音乐", L"spotify", L"bilibili", L"爱奇艺",
                       L"优酷", L"电影", L"影音", L"video lan", L"酷狗", L"酷我",
                       L"喜马拉雅", L"抖音", L"快手", L"腾讯视频", L"芒果",
                       L"央视频", L"cctv", L"kmplayer", L"foobar", L"aimp",
                       L"musicbee", L"咪咕", L"obs", L"直播", L"录屏", L"剪辑",
                       L"剪映", L"premiere", L"davinci", L"audacity"}},

        {L"社交聊天", {L"微信", L"wechat", L"qq", L"discord", L"telegram", L"钉钉",
                       L"企业微信", L"slack", L"社交", L"聊天", L"teams", L"飞书",
                       L"whatsapp", L"微博"}},

        {L"游戏", {L"steam", L"epic games", L"wegame", L"origin", L"battle.net",
                   L"uplay", L"riot", L"valorant", L"gta", L"grand theft auto",
                   L"minecraft", L"守望先锋", L"绝地求生", L"csgo", L"counter-strike",
                   L"dota", L"apex", L"fortnite", L"原神", L"genshin", L"崩坏",
                   L"honkai", L"星穹铁道", L"王者荣耀", L"和平精英", L"英雄联盟",
                   L"league of legends", L"lol", L"炉石", L"魔兽", L"暗黑",
                   L"暴雪", L"战网", L"育碧", L"playstation", L"xbox", L"game",
                   L"games", L"游戏", L"模拟器", L"emulator", L"战地",
                   L"battlefield", L"使命召唤", L"call of duty", L"永劫无间",
                   L"糖豆人", L"fall guys", L"赛博朋克", L"cyberpunk",
                   L"艾尔登法环", L"elden ring", L"黑暗之魂", L"dark souls",
                   L"极品飞车", L"need for speed", L"游戏加加"}},

        // 实用工具：下载/网盘/远控/压缩/驱动/加速等日常工具
        {L"实用工具", {L"idm", L"fdm", L"迅雷", L"thunder", L"aria2", L"motrix",
                       L"下载", L"download", L"everything", L"listary", L"utools",
                       L"quicker", L"snipaste", L"截图", L"sharex", L"picpick",
                       L"bandizip", L"7-zip", L"winrar", L"压缩", L"解压",
                       L"wallpaper", L"壁纸", L"向日葵", L"todesk", L"teamviewer",
                       L"anydesk", L"rustdesk", L"远控", L"clash", L"v2ray",
                       L"加速", L"百度网盘", L"阿里云盘", L"坚果云", L"onedrive",
                       L"dropbox", L"网盘", L"云盘", L"dock", L"驱动", L"driver",
                       L"鲁大师", L"aida64", L"hwmonitor", L"硬盘", L"磁盘",
                       L"清理", L"管家", L"360", L"沙盒", L"sandboxie", L"输入法",
                       L"搜狗", L"diskgenius", L"recuva", L"恢复", L"备份",
                       L"daemon", L"ultraiso", L"rufus", L"ventoy", L"刻录"}},
    };
    return rules;
}

std::wstring IconClassifier::ClassifyPath(const std::wstring& path) const {
    fs::path p(path);
    const std::wstring name = ToLowerCopy(p.stem().wstring());
    const std::wstring ext = ToLowerCopy(p.extension().wstring());

    if (fs::is_directory(p)) return ClassifyDirectory(name);

    const bool isApp = ext == L".lnk" || ext == L".exe" || ext == L".appref-ms" || ext == L".url";
    if (!isApp) return L"其他"; // 非应用文件（图片/音视频/文档等）

    for (const auto& rule : AppRules()) {
        if (ContainsAny(name, rule.keywords)) return rule.category;
    }
    return L"应用";
}

} // namespace desktopsticker
