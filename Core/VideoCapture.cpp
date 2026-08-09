#include "VideoCapture.h"
#include "FrameNavigation.h"
#include "FrameSourceState.h"
#include "ImageUtils.h"
#include "ImageState.h"
#include "ROIState.h"
#include "ToolChainState.h"
#include "RealtimeDetectionState.h"
#include "TemplateState.h"
#include "VisionContext.h"
#include "AudioPlayer.h"
#include "../Algorithm/ThresholdTool.h"
#include "../Algorithm/YOLODetector.h"
#include "../Log/LogSystem.h"

#include <opencv2/opencv.hpp>
#include <windows.h>
#include <vector>
#include <chrono>
#include <algorithm>

namespace VideoCapture
{

    static cv::VideoCapture s_Cap;
    static bool s_Open = false;
    static bool s_Playing = false;
    static bool s_IsCamera = false;
    static bool s_Loop = false;
    static std::string s_TempFile;  // 临时文件路径（Close 时删除）
    static int s_FrameCount = 0;
    static int s_CurrentFrame = 0;
    static double s_FPS = 30.0;

    // 播放计时
    static std::chrono::steady_clock::time_point s_LastFrameTime;

    // 前置声明
    static bool ReadFrame();

    bool OpenVideo(const std::string &path)
    {
        Close();

        // UTF-8 → 宽字符
        int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
        if (wlen <= 0) { LogSystem::Add(LOG_ERROR, "路径转换失败"); return false; }
        std::vector<wchar_t> wpath(wlen);
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);

        // 策略：直接 → 短名 → 临时文件
        std::string tempFile;
        auto tryOpen = [&](const std::string &p) {
            return s_Cap.open(p, cv::CAP_FFMPEG);  // 强制 FFmpeg 后端，避免 MSMF 中文 bug
        };

        // 1) 原始路径
        LogSystem::Add(LOG_INFO, "视频: 尝试直接打开 %s", path.c_str());
        if (tryOpen(path)) { LogSystem::Add(LOG_INFO, "视频: 直接打开成功"); goto opened; }
        LogSystem::Add(LOG_WARN, "视频: 直接打开失败");

        // 2) 8.3 短名
        {
            DWORD sl = GetShortPathNameW(wpath.data(), nullptr, 0);
            if (sl > 0) {
                std::vector<wchar_t> sw(sl);
                GetShortPathNameW(wpath.data(), sw.data(), sl);
                int u8l = WideCharToMultiByte(CP_UTF8, 0, sw.data(), -1, nullptr, 0, nullptr, nullptr);
                std::vector<char> sbuf(u8l);
                WideCharToMultiByte(CP_UTF8, 0, sw.data(), -1, sbuf.data(), u8l, nullptr, nullptr);
                LogSystem::Add(LOG_INFO, "视频: 尝试短名 %s", sbuf.data());
                if (tryOpen(sbuf.data())) { LogSystem::Add(LOG_INFO, "视频: 短名打开成功"); goto opened; }
                LogSystem::Add(LOG_WARN, "视频: 短名打开失败");
            }
        }

        // 3) 复制到 %TEMP% 纯 ASCII 名
        {
            wchar_t tmpDir[MAX_PATH], tmpName[MAX_PATH];
            if (!GetTempPathW(MAX_PATH, tmpDir))
            {
                LogSystem::Add(LOG_WARN, "视频: GetTempPathW 失败 (err=%lu)", GetLastError());
            }
            else if (!GetTempFileNameW(tmpDir, L"vid", 0, tmpName))
            {
                LogSystem::Add(LOG_WARN, "视频: GetTempFileNameW 失败 (err=%lu)", GetLastError());
            }
            else if (!CopyFileW(wpath.data(), tmpName, FALSE))
            {
                LogSystem::Add(LOG_WARN, "视频: CopyFileW 失败 (err=%lu)", GetLastError());
                DeleteFileW(tmpName);
            }
            else
            {
                int u8l = WideCharToMultiByte(CP_UTF8, 0, tmpName, -1, nullptr, 0, nullptr, nullptr);
                std::vector<char> tbuf(u8l);
                WideCharToMultiByte(CP_UTF8, 0, tmpName, -1, tbuf.data(), u8l, nullptr, nullptr);
                tempFile = tbuf.data();
                LogSystem::Add(LOG_INFO, "视频: 临时副本 %s", tempFile.c_str());
                if (tryOpen(tempFile))
                {
                    LogSystem::Add(LOG_INFO, "视频(→临时副本): %s", tempFile.c_str());
                    goto opened;
                }
                LogSystem::Add(LOG_WARN, "视频: 临时副本也无法打开");
                DeleteFileW(tmpName);
            }
        }

        LogSystem::Add(LOG_ERROR, "无法打开视频: %s", path.c_str());
        return false;

    opened:

        s_TempFile = tempFile;  // 记录临时文件以便关闭时清理
        s_Open = true;
        s_IsCamera = false;
        s_FrameCount = (int)s_Cap.get(cv::CAP_PROP_FRAME_COUNT);
        s_FPS = s_Cap.get(cv::CAP_PROP_FPS);
        if (s_FPS <= 0 || s_FPS > 120)
            s_FPS = 30.0;
        s_CurrentFrame = 0;
        s_Loop = false;

        LogSystem::Add(LOG_INFO, "视频已打开: %d帧, %.1ffps", s_FrameCount, s_FPS);

        // 立即抓取第一帧（不触发播放）
        ReadFrame();
        FrameNavigation::FitImageToWindow();
        ROIState::ClearInteraction();
TemplateState::ClearResults();

        // 打开音频流（如果有的话）
        AudioPlayer::Open(path);

        return true;
    }

    bool OpenCamera(int index)
    {
        Close();

        if (!s_Cap.open(index, cv::CAP_DSHOW))
        {
            // 回退：不用 DSHOW 后端
            if (!s_Cap.open(index))
            {
                LogSystem::Add(LOG_ERROR, "无法打开摄像头 #%d", index);
                return false;
            }
        }

        s_Open = true;
        s_IsCamera = true;
        s_FrameCount = 0;
        s_FPS = 30.0;
        s_CurrentFrame = 0;
        s_Loop = false;

        LogSystem::Add(LOG_INFO, "摄像头 #%d 已打开", index);

        ReadFrame();
        FrameNavigation::FitImageToWindow();
        ROIState::ClearInteraction();
TemplateState::ClearResults();
        Play(); // 摄像头自动开始播放

        return true;
    }

    void Close()
    {
        if (s_Open)
        {
            Pause();
            AudioPlayer::Close();
            s_Cap.release();
            s_Open = false;
            s_IsCamera = false;
            s_FrameCount = 0;
            s_CurrentFrame = 0;

            // 清理临时文件
            if (!s_TempFile.empty())
            {
                DeleteFileA(s_TempFile.c_str());
                s_TempFile.clear();
            }

            ToolChainState::SetYoloLiveDetect(false);
            ToolChainState::SetYoloLiveInstanceIndex(-1);
            FrameSourceState::Clear();
            ROIState::ClearInteraction();
            TemplateState::ClearResults();
            RealtimeDetectionState::Clear();
            gContext.ClearUnifiedResults();

            LogSystem::Add(LOG_INFO, "视频/摄像头已关闭");
        }
    }

    bool IsOpen() { return s_Open; }
    bool IsPlaying() { return s_Playing; }
    bool IsCamera() { return s_IsCamera; }

    void Play()
    {
        if (s_Open)
        {
            s_Playing = true;
            s_LastFrameTime = std::chrono::steady_clock::now();
            AudioPlayer::Play();
        }
    }

    void Pause()
    {
        s_Playing = false;
        AudioPlayer::Pause();
    }

    void TogglePlay()
    {
        if (s_Playing)
            Pause();
        else
            Play();
    }

    void Stop()
    {
        Pause();
        AudioPlayer::Stop();
        if (s_Open && !s_IsCamera)
        {
            s_CurrentFrame = 0;
            s_Cap.set(cv::CAP_PROP_POS_FRAMES, 0);
            ReadFrame(); // 回到第一帧
            FrameNavigation::FitImageToWindow();
        }
    }

    void SetLoop(bool loop) { s_Loop = loop; }
    bool IsLooping() { return s_Loop; }

    // =====================================================
    // 内部辅助：从 s_Cap 读取一帧并处理（不检查播放状态）
    // =====================================================
    static bool ReadFrame()
    {
        cv::Mat frame;
        if (!s_Cap.read(frame) || frame.empty())
        {
            // 视频结束
            if (!s_IsCamera)
            {
                if (s_Loop)
                {
                    s_CurrentFrame = 0;
                    s_Cap.set(cv::CAP_PROP_POS_FRAMES, 0);
                    AudioPlayer::Seek(0.0);
                    AudioPlayer::Play();
                    s_Cap.read(frame);
                    if (frame.empty())
                    {
                        Pause();
                        return false;
                    }
                }
                else
                {
                    Pause();
                    LogSystem::Add(LOG_INFO, "视频播放完毕");
                    return false;
                }
            }
            else
            {
                return false;
            }
        }

        // 更新帧计数
        if (!s_IsCamera)
            s_CurrentFrame = (int)s_Cap.get(cv::CAP_PROP_POS_FRAMES);
        else
            s_CurrentFrame++;

        // 更新计时
        if (s_Playing)
            s_LastFrameTime = std::chrono::steady_clock::now();

        // 摄像头画面水平镜像（自拍更自然）
        if (s_IsCamera)
            cv::flip(frame, frame, 1);

        FrameSourceState::SetCurrentFrame(frame,
            s_IsCamera ? FrameSourceType::Camera : FrameSourceType::Video,
            {},
            s_CurrentFrame,
            GetPositionSec() * 1000.0);

        // 转 RGBA → 标记 GPU 上传
        cv::Mat rgba;
        SafeConvertToRGBA(frame, rgba);

        ImageState::PendingUploadRef() = rgba;
        ImageState::NeedUploadRef() = true;

        return true;
    }

    bool Update()
    {
        if (!s_Open || !s_Playing)
            return false;

        // 播放速率控制
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - s_LastFrameTime).count();
        double interval = s_IsCamera ? (1.0 / 30.0) : (1.0 / s_FPS);
        if (elapsed < interval)
            return false;

        return ReadFrame();
    }

    int GetFrameCount() { return s_FrameCount; }
    int GetCurrentFrame() { return s_CurrentFrame; }
    double GetFPS() { return s_FPS; }
    double GetPositionSec()
    {
        if (s_FPS > 0)
            return s_CurrentFrame / s_FPS;
        return 0.0;
    }

    void SeekFrame(int frame)
    {
        if (!s_Open || s_IsCamera)
            return;

        frame = std::clamp(frame, 0, s_FrameCount - 1);
        s_Cap.set(cv::CAP_PROP_POS_FRAMES, frame);
        s_CurrentFrame = frame;
        ReadFrame();
        // 同步音频位置
        double seekSec = (double)frame / s_FPS;
        AudioPlayer::Seek(seekSec);
    }

} // namespace VideoCapture
