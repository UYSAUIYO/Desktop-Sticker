#include "pch.h"
#include "WeatherService.h"
#include "ClockWidget.h"

#include <nlohmann/json.hpp>
#include <wininet.h>

#include "desktopsticker/Log.h"
#include "desktopsticker/Utf8.h"

#pragma comment(lib, "wininet.lib")

namespace desktopsticker {

namespace {
const wchar_t kLocateBaidu[] =
    L"https://qifu-api.baidubce.com/ip/local/geo/v1/district";
const wchar_t kGeocodeFmt[] =
    L"https://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&language=zh&format=json";
const wchar_t kWeatherFmt[] =
    L"https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
    L"&current=temperature_2m,weather_code,wind_speed_10m,wind_direction_10m"
    L"&daily=sunrise,sunset&timezone=auto&forecast_days=1";

// 响应前 120 字节转可打印文本（非 ASCII 用 ? 占位），用于日志定位
std::wstring AsciiHead(const std::string& s) {
    std::wstring out;
    for (size_t i = 0; i < s.size() && i < 120; ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        out += (c >= 0x20 && c < 0x7F) ? static_cast<wchar_t>(c) : L'?';
    }
    return out;
}
} // namespace

void WeatherService::Start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this]() { Run(); });
}

void WeatherService::Stop() {
    if (!running_.exchange(false)) return;
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

WeatherInfo WeatherService::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return info_;
}

bool WeatherService::HttpGet(const std::wstring& url, std::string& out, bool direct) {
    // WinINet：自动跟随重定向、TLS 无坑（WinHTTP 在本机对 open-meteo 的 HTTPS
    // QueryDataAvailable 会返回 E_ABORT，已弃用）。
    // direct=true 用 INTERNET_OPEN_TYPE_DIRECT 直连：绕过系统代理取真实 IP 位置；
    // 天气按坐标查询与出口无关，走系统代理保证可达。
    bool ok = false;
    HINTERNET session = InternetOpenW(L"DesktopSticker/1.0",
                                      direct ? INTERNET_OPEN_TYPE_DIRECT : INTERNET_OPEN_TYPE_PRECONFIG,
                                      nullptr, nullptr, 0);
    HINTERNET handle = session ? InternetOpenUrlW(session, url.c_str(), nullptr, 0,
                                                  INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD,
                                                  0)
                               : nullptr;
    if (handle) {
        char buf[8192];
        DWORD read = 0;
        while (InternetReadFile(handle, buf, sizeof(buf), &read) && read > 0) {
            out.append(buf, read);
        }
        ok = !out.empty();
        InternetCloseHandle(handle);
    } else {
        dstklog::Write(L"weather", L"open url FAILED gle=" + std::to_wstring(GetLastError()));
    }
    if (session) InternetCloseHandle(session);
    return ok;
}

bool WeatherService::LocateIpApi(bool direct, double& lat, double& lon,
                                 std::wstring& country, std::wstring& region, std::wstring& city) {
    std::string body;
    if (!HttpGet(direct
                     ? L"http://ip-api.com/json/?lang=zh-CN&fields=status,country,regionName,city,lat,lon"
                     : L"http://ip-api.com/json/?lang=zh-CN&fields=status,country,regionName,city,lat,lon",
                 body, direct)) {
        return false;
    }
    auto j = nlohmann::json::parse(body, nullptr, false);
    if (j.is_discarded() || j.value("status", "") != "success") {
        dstklog::Write(L"weather", L"ip-api fail size=" + std::to_wstring(body.size()) +
                                        L" body=" + AsciiHead(body));
        return false;
    }
    const std::wstring c = FromUtf8(j.value("city", ""));
    if (c.empty()) return false;
    lat = j.value("lat", 0.0);
    lon = j.value("lon", 0.0);
    country = FromUtf8(j.value("country", std::string()));
    region = FromUtf8(j.value("regionName", std::string()));
    city = c;
    return true;
}

bool WeatherService::GeocodeCity(const std::wstring& city, double& lat, double& lon, std::wstring& country) {
    wchar_t url[512]{};
    swprintf_s(url, kGeocodeFmt, city.c_str());
    // 城市名来自真实 IP：先直连，直连不可达再走代理
    std::string body;
    if (!HttpGet(url, body, /*direct=*/true) && !HttpGet(url, body, /*direct=*/false)) {
        dstklog::Write(L"weather", L"geocode http FAILED (direct+proxy)");
        return false;
    }
    auto j = nlohmann::json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.contains("results") || !j["results"].is_array() ||
        j["results"].empty()) {
        dstklog::Write(L"weather", L"geocode no-result size=" + std::to_wstring(body.size()) +
                                        L" body=" + AsciiHead(body));
        return false;
    }
    const auto& r = j["results"][0];
    lat = r.value("latitude", 0.0);
    lon = r.value("longitude", 0.0);
    country = FromUtf8(r.value("country", ""));
    return lat != 0.0 || lon != 0.0;
}

bool WeatherService::LocateBaidu(bool direct, double& lat, double& lon,
                                 std::wstring& country, std::wstring& region, std::wstring& city) {
    std::string body;
    if (!HttpGet(kLocateBaidu, body, direct)) return false;
    auto j = nlohmann::json::parse(body, nullptr, false);
    if (j.is_discarded() || j.value("code", "") != "Success" || !j.contains("data")) {
        dstklog::Write(L"weather", L"baidu parse/code fail size=" + std::to_wstring(body.size()));
        return false;
    }
    const auto& data = j["data"];
    std::wstring c = FromUtf8(data.value("city", ""));
    if (c.empty()) {
        dstklog::Write(L"weather", L"baidu: empty city, data=" + AsciiHead(body));
        return false;
    }
    dstklog::Write(L"weather", L"baidu city=" + c);
    // Open-Meteo 地理编码按基础名检索（"湛江市" → "湛江"）
    if (!c.empty() && c.back() == L'市') c.pop_back();
    std::wstring geoCountry;
    if (!GeocodeCity(c, lat, lon, geoCountry)) return false;
    country = geoCountry.empty() ? FromUtf8(data.value("country", "")) : geoCountry;
    region = FromUtf8(data.value("prov", ""));
    city = c;
    return true;
}

bool WeatherService::FetchWeather(double lat, double lon, WeatherInfo& out) {
    wchar_t url[512]{};
    swprintf_s(url, kWeatherFmt, lat, lon);
    std::string body;
    if (!HttpGet(url, body)) {
        dstklog::Write(L"weather", L"weather http FAILED");
        return false;
    }
    auto j = nlohmann::json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.contains("current")) {
        dstklog::Write(L"weather", L"weather parse fail size=" + std::to_wstring(body.size()) +
                                        L" body=" + AsciiHead(body));
        return false;
    }
    const auto& cur = j["current"];
    out.temp = cur.value("temperature_2m", 0.0);
    out.weatherCode = cur.value("weather_code", -1);
    out.windSpeed = cur.value("wind_speed_10m", 0.0);
    out.windDir = cur.value("wind_direction_10m", 0);
    if (j.contains("daily") && j["daily"].contains("sunrise") &&
        j["daily"]["sunrise"].is_array() && j["daily"]["sunrise"].size() > 0) {
        // "2026-08-30T05:32" → "05:32"
        const std::string sr = j["daily"]["sunrise"][0].get<std::string>();
        const std::string ss = j["daily"]["sunset"][0].get<std::string>();
        if (sr.size() >= 16 && ss.size() >= 16) {
            out.sunrise = FromUtf8(sr.substr(11, 5));
            out.sunset = FromUtf8(ss.substr(11, 5));
            out.sunriseMin = ClockText::MinutesOfDay(out.sunrise);
            out.sunsetMin = ClockText::MinutesOfDay(out.sunset);
        }
    }
    return out.weatherCode >= 0;
}

void WeatherService::Run() {
    bool located = false;
    double lat = 0, lon = 0;
    std::wstring country, region, city;
    int failures = 0;

    for (;;) {
        WeatherInfo local;
        if (!located) {
            // 定位一律直连（绕过代理）：取真实 IP 的位置
            if (LocateIpApi(/*direct=*/true, lat, lon, country, region, city) ||
                LocateBaidu(/*direct=*/true, lat, lon, country, region, city)) {
                located = true;
                dstklog::Write(L"weather", L"located: " + country + L" " + region + L" " + city);
            } else {
                dstklog::Write(L"weather", L"locate failed (both sources, direct)");
            }
        }
        if (located) {
            local.located = true;
            local.country = country;
            local.region = region;
            local.city = city;
            if (FetchWeather(lat, lon, local)) {
                failures = 0;
                dstklog::Write(L"weather", L"weather ok code=" + std::to_wstring(local.weatherCode));
            } else {
                ++failures;
                dstklog::Write(L"weather", L"weather fetch FAILED");
            }
        } else {
            ++failures;
        }
        local.valid = located && failures == 0;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            info_ = local;
        }

        std::unique_lock<std::mutex> lock(mutex_);
        if (failures == 0) {
            cv_.wait_for(lock, std::chrono::minutes(30), [this]() { return !running_.load(); });
        } else {
            // 失败快速重试（前 6 次 20 秒），持续失败放宽到 10 分钟
            const auto wait = failures <= 6 ? std::chrono::seconds(20) : std::chrono::minutes(10);
            cv_.wait_for(lock, wait, [this]() { return !running_.load(); });
        }
        if (!running_.load()) break;
    }
}

} // namespace desktopsticker
