#include "FrameArchiveService.h"

#include <opencv2/imgcodecs.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

namespace
{
namespace fs = std::filesystem;

struct ArchiveJob
{
    cv::Mat frame;
    FrameArchiveConfig config;
    std::string sourceName;
    int frameIndex = -1;
    double timestampMs = 0.0;
};

std::mutex s_mutex;
std::condition_variable s_condition;
std::condition_variable s_idleCondition;
std::deque<ArchiveJob> s_queue;
std::thread s_worker;
FrameArchiveConfig s_config;
FrameArchiveSnapshot s_stats;
bool s_initialized = false;
bool s_stop = false;
bool s_workerBusy = false;

fs::path Utf8Path(const std::string& value)
{
    const std::u8string utf8(reinterpret_cast<const char8_t*>(value.data()), value.size());
    return fs::path(utf8);
}

std::string PathToUtf8(const fs::path& value)
{
    const std::u8string utf8 = value.u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

fs::path AppDataDirectory()
{
    wchar_t* localAppData = nullptr;
    std::size_t length = 0;
    if (_wdupenv_s(&localAppData, &length, L"LOCALAPPDATA") == 0 &&
        localAppData && length > 1)
    {
        const fs::path path = fs::path(localAppData) / L"IMgui_Opencv";
        std::free(localAppData);
        return path;
    }
    std::free(localAppData);
    return fs::current_path();
}

std::string DefaultCaptureDirectory()
{
    wchar_t* userProfile = nullptr;
    std::size_t length = 0;
    if (_wdupenv_s(&userProfile, &length, L"USERPROFILE") == 0 &&
        userProfile && length > 1)
    {
        const std::string path = PathToUtf8(
            fs::path(userProfile) / L"Pictures" / L"IMgui_Opencv" / L"Captures");
        std::free(userProfile);
        return path;
    }
    std::free(userProfile);
    return PathToUtf8(fs::current_path() / L"captures");
}

fs::path SettingsFile()
{
    return AppDataDirectory() / L"capture_settings.json";
}

FrameArchiveConfig NormalizeConfig(FrameArchiveConfig config)
{
    if (config.directory.empty())
        config.directory = DefaultCaptureDirectory();
    config.jpegQuality = std::clamp(config.jpegQuality, 20, 100);
    config.saveEveryN = std::clamp(config.saveEveryN, 1, 10000);
    config.maxQueue = std::clamp(config.maxQueue, 1, 64);
    const int format = std::clamp(static_cast<int>(config.format), 0, 2);
    config.format = static_cast<FrameArchiveFormat>(format);
    return config;
}

FrameArchiveConfig LoadConfigFromDisk()
{
    FrameArchiveConfig config;
    config.directory = DefaultCaptureDirectory();
    try
    {
        std::ifstream input(SettingsFile(), std::ios::binary);
        if (!input)
            return NormalizeConfig(std::move(config));
        nlohmann::json json;
        input >> json;
        config.enabled = json.value("enabled", false);
        config.directory = json.value("directory", config.directory);
        config.format = static_cast<FrameArchiveFormat>(json.value("format", 0));
        config.jpegQuality = json.value("jpegQuality", 95);
        config.saveEveryN = json.value("saveEveryN", 1);
        config.maxQueue = json.value("maxQueue", 8);
    }
    catch (...)
    {
    }
    return NormalizeConfig(std::move(config));
}

bool SaveConfigToDisk(const FrameArchiveConfig& config, std::string& error)
{
    try
    {
        fs::create_directories(SettingsFile().parent_path());
        nlohmann::json json = {
            {"enabled", config.enabled},
            {"directory", config.directory},
            {"format", static_cast<int>(config.format)},
            {"jpegQuality", config.jpegQuality},
            {"saveEveryN", config.saveEveryN},
            {"maxQueue", config.maxQueue}
        };
        std::ofstream output(SettingsFile(), std::ios::binary | std::ios::trunc);
        if (!output)
        {
            error = "无法写入采集保存配置";
            return false;
        }
        output << json.dump(2);
        return static_cast<bool>(output);
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
}

std::string SanitizeSourceName(const std::string& sourceName)
{
    std::string sanitized;
    sanitized.reserve(sourceName.size());
    for (unsigned char ch : sourceName)
    {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_')
        {
            sanitized.push_back(static_cast<char>(ch));
        }
        else if (!sanitized.empty() && sanitized.back() != '_')
        {
            sanitized.push_back('_');
        }
    }
    return sanitized.empty() ? "camera" : sanitized;
}

const char* Extension(FrameArchiveFormat format)
{
    switch (format)
    {
    case FrameArchiveFormat::Png: return ".png";
    case FrameArchiveFormat::Bmp: return ".bmp";
    default: return ".jpg";
    }
}

fs::path BuildOutputPath(const ArchiveJob& job)
{
    const auto millis = static_cast<std::int64_t>(job.timestampMs > 0.0
        ? job.timestampMs
        : std::chrono::duration<double, std::milli>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    const std::time_t seconds = static_cast<std::time_t>(millis / 1000);
    std::tm localTime{};
    localtime_s(&localTime, &seconds);

    std::ostringstream name;
    name << SanitizeSourceName(job.sourceName) << '_'
         << std::put_time(&localTime, "%Y%m%d_%H%M%S") << '_'
         << std::setw(3) << std::setfill('0') << (millis % 1000) << "_f"
         << std::setw(8) << std::setfill('0') << std::max(0, job.frameIndex)
         << Extension(job.config.format);
    return Utf8Path(job.config.directory) / Utf8Path(name.str());
}

bool SaveJob(const ArchiveJob& job, std::string& savedPath, std::string& error)
{
    try
    {
        const fs::path outputPath = BuildOutputPath(job);
        fs::create_directories(outputPath.parent_path());

        std::vector<int> parameters;
        if (job.config.format == FrameArchiveFormat::Jpeg)
            parameters = {cv::IMWRITE_JPEG_QUALITY, job.config.jpegQuality};

        std::vector<unsigned char> encoded;
        if (!cv::imencode(Extension(job.config.format), job.frame, encoded, parameters))
        {
            error = "图像编码失败";
            return false;
        }

        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            error = "无法创建采集图像文件";
            return false;
        }
        output.write(reinterpret_cast<const char*>(encoded.data()),
            static_cast<std::streamsize>(encoded.size()));
        if (!output)
        {
            error = "写入采集图像失败";
            return false;
        }
        savedPath = PathToUtf8(outputPath);
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
}

void WorkerLoop()
{
    for (;;)
    {
        ArchiveJob job;
        {
            std::unique_lock<std::mutex> lock(s_mutex);
            s_condition.wait(lock, [] { return s_stop || !s_queue.empty(); });
            if (s_stop && s_queue.empty())
                break;
            job = std::move(s_queue.front());
            s_queue.pop_front();
            s_workerBusy = true;
        }

        std::string savedPath;
        std::string error;
        const bool saved = SaveJob(job, savedPath, error);

        {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_workerBusy = false;
            if (saved)
            {
                ++s_stats.savedFrames;
                s_stats.lastSavedPath = std::move(savedPath);
                s_stats.lastError.clear();
            }
            else
            {
                ++s_stats.failedFrames;
                s_stats.lastError = std::move(error);
            }
            if (s_queue.empty())
                s_idleCondition.notify_all();
        }
    }
}
}

namespace FrameArchiveService
{
void Initialize()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_initialized)
        return;
    s_config = LoadConfigFromDisk();
    s_stats = {};
    s_stats.config = s_config;
    s_stop = false;
    s_workerBusy = false;
    s_initialized = true;
    s_worker = std::thread(WorkerLoop);
}

FrameArchiveConfig Config()
{
    Initialize();
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_config;
}

void Configure(const FrameArchiveConfig& rawConfig, bool persist)
{
    Initialize();
    const FrameArchiveConfig config = NormalizeConfig(rawConfig);
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_config = config;
        s_stats.config = config;
        if (!config.enabled && !s_queue.empty())
        {
            s_stats.droppedFrames += s_queue.size();
            s_queue.clear();
            s_idleCondition.notify_all();
        }
    }
    if (persist)
    {
        std::string error;
        if (!SaveConfigToDisk(config, error))
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_stats.lastError = std::move(error);
        }
    }
}

void Enqueue(const cv::Mat& frame, const std::string& sourceName,
    int frameIndex, double timestampMs)
{
    if (frame.empty())
        return;
    Initialize();

    FrameArchiveConfig config;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        ++s_stats.receivedFrames;
        config = s_config;
        if (!config.enabled ||
            ((s_stats.receivedFrames - 1) % static_cast<std::uint64_t>(config.saveEveryN)) != 0)
        {
            return;
        }
    }

    ArchiveJob job;
    job.frame = frame.clone();
    job.config = config;
    job.sourceName = sourceName;
    job.frameIndex = frameIndex;
    job.timestampMs = timestampMs;

    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_config.enabled)
            return;
        while (s_queue.size() >= static_cast<std::size_t>(config.maxQueue))
        {
            s_queue.pop_front();
            ++s_stats.droppedFrames;
        }
        s_queue.push_back(std::move(job));
        ++s_stats.queuedFrames;
    }
    s_condition.notify_one();
}

FrameArchiveSnapshot Snapshot()
{
    Initialize();
    std::lock_guard<std::mutex> lock(s_mutex);
    FrameArchiveSnapshot snapshot = s_stats;
    snapshot.config = s_config;
    snapshot.pendingFrames = s_queue.size() + (s_workerBusy ? 1u : 0u);
    snapshot.workerRunning = s_initialized && !s_stop;
    return snapshot;
}

bool WaitUntilIdle(int timeoutMs)
{
    Initialize();
    std::unique_lock<std::mutex> lock(s_mutex);
    return s_idleCondition.wait_for(lock, std::chrono::milliseconds(std::max(0, timeoutMs)),
        [] { return s_queue.empty() && !s_workerBusy; });
}

std::string SettingsPath()
{
    return PathToUtf8(SettingsFile());
}

void Shutdown()
{
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_initialized)
            return;
        s_stop = true;
    }
    s_condition.notify_all();
    if (s_worker.joinable())
        s_worker.join();

    std::lock_guard<std::mutex> lock(s_mutex);
    s_queue.clear();
    s_initialized = false;
    s_stop = false;
    s_workerBusy = false;
}
}
