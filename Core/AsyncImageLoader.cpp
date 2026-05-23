#include "AsyncImageLoader.h"
#include "../Log/LogSystem.h"
#include <vector>

// =========================
// 内部状态（仅此文件可见）
// =========================
static std::future<cv::Mat> s_Future;
static bool                  s_Pending = false;
static std::mutex            s_Mutex;
static std::string           s_RequestPath;

namespace AsyncImageLoader
{

void RequestLoad(const std::string& path)
{
    std::lock_guard<std::mutex> lock(s_Mutex);

    // 如果已有任务在进行，先取消
    if (s_Pending && s_Future.valid())
    {
        // 不等待，直接丢弃旧 future（后台线程会自行结束）
        s_Future = {};
    }

    s_RequestPath = path;
    s_Pending = true;

    LogSystem::Add(LOG_INFO, "开始异步加载图片...");

    s_Future = std::async(std::launch::async, [](const std::string& p) -> cv::Mat {
        // 后台线程：UTF-8 → 宽字符路径
        int wlen = MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, nullptr, 0);
        if (wlen <= 0) return {};

        std::vector<wchar_t> wpath(wlen);
        MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, wpath.data(), wlen);

        // 读文件
        FILE* fp = nullptr;
        if (_wfopen_s(&fp, wpath.data(), L"rb") != 0 || !fp) return {};

        fseek(fp, 0, SEEK_END);
        size_t size = ftell(fp);
        rewind(fp);
        if (size == 0 || size > 512 * 1024 * 1024)
        {
            fclose(fp);
            return {};
        }

        std::vector<uchar> data(size);
        fread(data.data(), 1, size, fp);
        fclose(fp);

        // OpenCV 解码
        return cv::imdecode(data, cv::IMREAD_COLOR);
    }, path);
}

bool CheckAndProcess(void (*callback)(cv::Mat img))
{
    std::lock_guard<std::mutex> lock(s_Mutex);

    if (!s_Pending || !s_Future.valid())
        return false;

    auto status = s_Future.wait_for(std::chrono::milliseconds(0));
    if (status != std::future_status::ready)
        return false;

    cv::Mat img = s_Future.get();
    s_Pending = false;

    if (img.empty())
    {
        LogSystem::Add(LOG_ERROR, "异步加载图片失败");
        return true;
    }

    if (callback)
        callback(img);

    LogSystem::Add(LOG_INFO, "异步图片加载完成: %dx%d", img.cols, img.rows);
    return true;
}

bool IsPending()
{
    std::lock_guard<std::mutex> lock(s_Mutex);
    return s_Pending;
}

void Cancel()
{
    std::lock_guard<std::mutex> lock(s_Mutex);
    if (s_Pending && s_Future.valid())
        s_Future = {};
    s_Pending = false;
    s_RequestPath.clear();
}

} // namespace AsyncImageLoader
