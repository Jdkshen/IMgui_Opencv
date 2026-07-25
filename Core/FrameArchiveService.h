#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <opencv2/core/mat.hpp>

enum class FrameArchiveFormat
{
    Jpeg = 0,
    Png = 1,
    Bmp = 2
};

struct FrameArchiveConfig
{
    bool enabled = false;
    std::string directory;
    FrameArchiveFormat format = FrameArchiveFormat::Jpeg;
    int jpegQuality = 95;
    int saveEveryN = 1;
    int maxQueue = 8;
};

struct FrameArchiveSnapshot
{
    FrameArchiveConfig config;
    std::uint64_t receivedFrames = 0;
    std::uint64_t queuedFrames = 0;
    std::uint64_t savedFrames = 0;
    std::uint64_t droppedFrames = 0;
    std::uint64_t failedFrames = 0;
    std::size_t pendingFrames = 0;
    bool workerRunning = false;
    std::string lastSavedPath;
    std::string lastError;
};

namespace FrameArchiveService
{
    void Initialize();
    FrameArchiveConfig Config();
    void Configure(const FrameArchiveConfig& config, bool persist = true);
    void Enqueue(const cv::Mat& frame, const std::string& sourceName,
        int frameIndex, double timestampMs);
    FrameArchiveSnapshot Snapshot();
    bool WaitUntilIdle(int timeoutMs);
    std::string SettingsPath();
    void Shutdown();
}
