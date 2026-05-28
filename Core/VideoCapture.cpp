#include "VideoCapture.h"
#include "DX12Context.h"
#include "OpenCVTest.h"
#include "AudioPlayer.h"
#include "../Algorithm/ThresholdTool.h"
#include "../UI/ImageViewer.h"
#include "../UI/ROIManager.h"
#include "../Algorithm/TemplateMatch.h"
#include "../Log/LogSystem.h"

#include <opencv2/opencv.hpp>
#include <windows.h>
#include <vector>
#include <chrono>
#include <algorithm>

namespace VideoCapture
{

static cv::VideoCapture s_Cap;
static bool             s_Open       = false;
static bool             s_Playing    = false;
static bool             s_IsCamera   = false;
static bool             s_Loop       = false;
static int              s_FrameCount = 0;
static int              s_CurrentFrame = 0;
static double           s_FPS        = 30.0;

// 播放计时
static std::chrono::steady_clock::time_point s_LastFrameTime;

// 前置声明
static bool ReadFrame();

bool OpenVideo(const std::string& path)
{
    Close();

    // UTF-8 → 宽字符 → 短路径名（兼容中文路径，OpenCV FFmpeg/MSMF 需要 ANSI 路径）
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen <= 0)
    {
        LogSystem::Add(LOG_ERROR, "路径转换失败: %s", path.c_str());
        return false;
    }
    std::vector<wchar_t> wpath(wlen);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);

    // 获取短路径名（8.3 格式，纯 ASCII，避免中文问号问题）
    DWORD shortLen = GetShortPathNameW(wpath.data(), nullptr, 0);
    std::string shortPath;
    if (shortLen > 0)
    {
        std::vector<wchar_t> shortW(shortLen);
        GetShortPathNameW(wpath.data(), shortW.data(), shortLen);
        // 短路径转回 UTF-8
        int slen = WideCharToMultiByte(CP_UTF8, 0, shortW.data(), -1, nullptr, 0, nullptr, nullptr);
        if (slen > 0)
        {
            std::vector<char> sbuf(slen);
            WideCharToMultiByte(CP_UTF8, 0, shortW.data(), -1, sbuf.data(), slen, nullptr, nullptr);
            shortPath = sbuf.data();
        }
    }

    // 优先用短路径，回退到原始路径
    const std::string& openPath = shortPath.empty() ? path : shortPath;
    if (!s_Cap.open(openPath))
    {
        LogSystem::Add(LOG_ERROR, "无法打开视频: %s", path.c_str());
        return false;
    }

    s_Open       = true;
    s_IsCamera   = false;
    s_FrameCount = (int)s_Cap.get(cv::CAP_PROP_FRAME_COUNT);
    s_FPS        = s_Cap.get(cv::CAP_PROP_FPS);
    if (s_FPS <= 0 || s_FPS > 120) s_FPS = 30.0;
    s_CurrentFrame = 0;
    s_Loop       = false;

    LogSystem::Add(LOG_INFO, "视频已打开: %d帧, %.1ffps", s_FrameCount, s_FPS);

    // 立即抓取第一帧（不触发播放）
    ReadFrame();
    UI::FitImageToWindow();
    UI::ClearROIState();
    TemplateMatch::Clear();

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

    s_Open       = true;
    s_IsCamera   = true;
    s_FrameCount = 0;
    s_FPS        = 30.0;
    s_CurrentFrame = 0;
    s_Loop       = false;

    LogSystem::Add(LOG_INFO, "摄像头 #%d 已打开", index);

    ReadFrame();
    UI::FitImageToWindow();
    UI::ClearROIState();
    TemplateMatch::Clear();
    Play();  // 摄像头自动开始播放

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

        // 清除画面显示
        UI::ClearImage();

        LogSystem::Add(LOG_INFO, "视频/摄像头已关闭");
    }
}

bool IsOpen()     { return s_Open; }
bool IsPlaying()  { return s_Playing; }
bool IsCamera()   { return s_IsCamera; }

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
    if (s_Playing) Pause();
    else           Play();
}

void Stop()
{
    Pause();
    AudioPlayer::Stop();
    if (s_Open && !s_IsCamera)
    {
        s_CurrentFrame = 0;
        s_Cap.set(cv::CAP_PROP_POS_FRAMES, 0);
        ReadFrame();  // 回到第一帧
        UI::FitImageToWindow();
    }
}

void SetLoop(bool loop) { s_Loop = loop; }
bool IsLooping()        { return s_Loop; }

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
                s_Cap.read(frame);
                if (frame.empty()) { Pause(); return false; }
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

    // 保存原始 BGR 帧到 gImage（供像素读取和处理工具使用）
    gImage = frame.clone();
    gOriginalImage = gImage;
    gImageWidth = frame.cols;
    gImageHeight = frame.rows;

    // 转 RGBA → 标记 GPU 上传
    cv::Mat rgba;
    int ch = frame.channels();
    if (ch == 4)
        cv::cvtColor(frame, rgba, cv::COLOR_BGRA2RGBA);
    else if (ch == 3)
        cv::cvtColor(frame, rgba, cv::COLOR_BGR2RGBA);
    else
        cv::cvtColor(frame, rgba, cv::COLOR_GRAY2RGBA);

    gPendingUpload = rgba;
    gNeedUpload = true;

    return true;
}

bool Update()
{
    if (!s_Open || !s_Playing) return false;

    // 播放速率控制
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - s_LastFrameTime).count();
    double interval = s_IsCamera ? (1.0 / 30.0) : (1.0 / s_FPS);
    if (elapsed < interval)
        return false;

    return ReadFrame();
}

int    GetFrameCount()   { return s_FrameCount; }
int    GetCurrentFrame() { return s_CurrentFrame; }
double GetFPS()          { return s_FPS; }
double GetPositionSec()
{
    if (s_FPS > 0)
        return s_CurrentFrame / s_FPS;
    return 0.0;
}

void SeekFrame(int frame)
{
    if (!s_Open || s_IsCamera) return;

    frame = std::clamp(frame, 0, s_FrameCount - 1);
    s_Cap.set(cv::CAP_PROP_POS_FRAMES, frame);
    s_CurrentFrame = frame;
    ReadFrame();
    // 同步音频位置
    double seekSec = (double)frame / s_FPS;
    AudioPlayer::Seek(seekSec);
}

} // namespace VideoCapture
