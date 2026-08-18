#include "pch.h"
#include "desktopsticker/PinyinMapper.h"

#include <algorithm>
#include <cctype>

namespace desktopsticker {

const std::unordered_map<wchar_t, wchar_t>& PinyinMapper::Table() {
    static const std::unordered_map<wchar_t, wchar_t> table = {
        {L'微', L'w'}, {L'信', L'x'}, {L'浏', L'l'}, {L'览', L'l'}, {L'器', L'q'},
        {L'文', L'w'}, {L'档', L'd'}, {L'图', L't'}, {L'片', L'p'}, {L'视', L's'},
        {L'频', L'p'}, {L'音', L'y'}, {L'乐', L'l'}, {L'游', L'y'}, {L'戏', L'x'},
        {L'桌', L'z'}, {L'面', L'm'}, {L'设', L's'}, {L'置', L'z'}, {L'聊', L'l'},
        {L'天', L't'}, {L'工', L'g'}, {L'作', L'z'}, {L'娱', L'y'}, {L'电', L'd'},
        {L'脑', L'n'}, {L'管', L'g'}, {L'理', L'l'}, {L'记', L'j'}, {L'事', L's'},
        {L'本', L'b'}, {L'计', L'j'}, {L'算', L's'}, {L'机', L'j'}, {L'播', L'b'},
        {L'放', L'f'}, {L'相', L'x'}, {L'册', L'c'}, {L'下', L'x'}, {L'载', L'z'},
        {L'件', L'j'}, {L'夹', L'j'}, {L'资', L'z'}, {L'源', L'y'},
        {L'歌', L'g'}, {L'曲', L'q'}, {L'照', L'z'}, {L'录', L'l'}, {L'屏', L'p'},
        {L'幕', L'm'}, {L'键', L'j'}, {L'盘', L'p'}, {L'鼠', L's'}, {L'标', L'b'},
        {L'网', L'w'}, {L'页', L'y'}, {L'邮', L'y'}, {L'箱', L'x'}, {L'日', L'r'},
        {L'历', L'l'}, {L'时', L's'}, {L'钟', L'z'}, {L'地', L'd'},
        {L'导', L'd'}, {L'航', L'h'}, {L'剪', L'j'}, {L'贴', L't'}, {L'板', L'b'},
    };
    return table;
}

std::wstring PinyinMapper::GetInitials(const std::wstring& text) {
    const auto& table = Table();
    std::wstring result;
    result.reserve(text.size());
    for (wchar_t ch : text) {
        auto it = table.find(ch);
        if (it != table.end()) {
            result.push_back(it->second);
        } else if (ch >= L'A' && ch <= L'Z') {
            result.push_back(static_cast<wchar_t>(std::towlower(ch)));
        } else if ((ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9')) {
            result.push_back(ch);
        }
        // 其他字符（标点/空格）忽略
    }
    return result;
}

} // namespace desktopsticker
