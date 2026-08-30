#pragma once
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace desktopsticker {

struct WeatherInfo {
    bool located = false;       // 已解析出国家/城市与经纬度
    bool valid = false;         // 天气数据有效（离线/接口失败时为 false，UI 显示占位）
    std::wstring country;
    std::wstring region;        // 省/州级行政区
    std::wstring city;
    double temp = 0;            // 当前气温 ℃
    int weatherCode = -1;       // WMO 天气代码
    double windSpeed = 0;       // 风速 m/s
    int windDir = 0;            // 风向（度）
    std::wstring sunrise = L"--:--";
    std::wstring sunset = L"--:--";
    int sunriseMin = -1;        // 日出分钟数（弧线进度用）
    int sunsetMin = -1;
};

// 天气服务：IP 定位 + Open-Meteo 当前天气与日出日落（均免费、无密钥）。
// 定位链：ip-api（带经纬度）失败则用百度 qifu 取城市，再经 Open-Meteo 地理编码换经纬度。
// 定位请求走 INTERNET_OPEN_TYPE_DIRECT 直连（绕过代理，取真实 IP 的位置）；
// 天气请求按坐标查询，走系统代理以保证可达。失败 20 秒重试，成功 30 分钟刷新。
class WeatherService {
public:
    void Start();
    void Stop();
    WeatherInfo Snapshot() const;

private:
    void Run();
    static bool HttpGet(const std::wstring& url, std::string& out, bool direct = false);
    static bool LocateIpApi(bool direct, double& lat, double& lon,
                            std::wstring& country, std::wstring& region, std::wstring& city);
    static bool LocateBaidu(bool direct, double& lat, double& lon,
                            std::wstring& country, std::wstring& region, std::wstring& city);
    static bool GeocodeCity(const std::wstring& city, double& lat, double& lon, std::wstring& country);
    static bool FetchWeather(double lat, double lon, WeatherInfo& out);

    mutable std::mutex mutex_;
    WeatherInfo info_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::condition_variable cv_;
};

} // namespace desktopsticker
