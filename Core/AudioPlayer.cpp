#include "AudioPlayer.h"
#include "../Log/LogSystem.h"

#define NOMINMAX
#include <windows.h>
#include <xaudio2.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <atomic>
#include <vector>
#include <thread>
#include <chrono>

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace AudioPlayer
{

// =====================================================
// XAudio2 引擎 & 主控声音
// =====================================================
static IXAudio2*               s_XAudio2   = nullptr;
static IXAudio2MasteringVoice* s_MasterVoice = nullptr;
static IXAudio2SourceVoice*    s_SourceVoice = nullptr;

// =====================================================
// Media Foundation 源读取器
// =====================================================
static IMFSourceReader* s_Reader = nullptr;
static WAVEFORMATEX     s_WaveFmt = {};

// =====================================================
// 播放状态 & 后台线程
// =====================================================
static std::atomic<bool> s_Open      = false;
static std::atomic<bool> s_Playing   = false;
static std::atomic<bool> s_Eof       = false;
static std::atomic<bool> s_ExitThread = false;
static std::thread       s_Thread;
static HANDLE            s_WakeEvent = nullptr;

static std::atomic<float> s_Volume    = 1.0f;
static std::atomic<float> s_Rate      = 1.0f;
static std::atomic<double> s_SeekTarget = -1.0;
static std::atomic<double> s_Position   = 0.0;

// 音频样本缓冲池（固定大小，避免每帧分配）
struct AudioBuffer
{
    std::vector<BYTE> data;
    XAUDIO2_BUFFER    xaBuf = {};
};
static AudioBuffer s_Buffers[3];
static int         s_BufIdx = 0;

// =====================================================
// 初始化 XAudio2
// =====================================================
static bool InitXAudio2()
{
    if (s_XAudio2) return true;

    HRESULT hr = XAudio2Create(&s_XAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr))
    {
        LogSystem::Add(LOG_ERROR, "XAudio2Create 失败: 0x%08X", hr);
        return false;
    }

    hr = s_XAudio2->CreateMasteringVoice(&s_MasterVoice, 2, 44100, 0, nullptr, nullptr);
    if (FAILED(hr))
    {
        LogSystem::Add(LOG_ERROR, "CreateMasteringVoice 失败: 0x%08X", hr);
        s_XAudio2->Release();
        s_XAudio2 = nullptr;
        return false;
    }

    return true;
}

// =====================================================
// 初始化 Media Foundation
// =====================================================
static bool s_MFInitialized = false;
static bool InitMF()
{
    if (s_MFInitialized) return true;
    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr))
    {
        LogSystem::Add(LOG_ERROR, "MFStartup 失败: 0x%08X", hr);
        return false;
    }
    s_MFInitialized = true;
    return true;
}

// 前置声明
static void FeedAudio();

// =====================================================
// 打开文件：创建 IMFSourceReader，配置音频流
// =====================================================
bool Open(const std::string& path)
{
    Close();

    if (!InitMF() || !InitXAudio2()) return false;

    // UTF-8 → 宽字符
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return false;
    std::vector<wchar_t> wpath(wlen);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);

    HRESULT hr = MFCreateSourceReaderFromURL(wpath.data(), nullptr, &s_Reader);
    if (FAILED(hr))
    {
        // 文件可能没有音频流，不算致命错误
        LogSystem::Add(LOG_WARN, "音频无法打开（文件可能无音频流）: %s", path.c_str());
        return false;
    }

    // 选择第一个音频流，输出 PCM 格式
    IMFMediaType* pPartialType = nullptr;
    MFCreateMediaType(&pPartialType);
    pPartialType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pPartialType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);

    hr = s_Reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    hr = s_Reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
    hr = s_Reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pPartialType);
    pPartialType->Release();

    if (FAILED(hr))
    {
        s_Reader->Release();
        s_Reader = nullptr;
        return false;
    }

    // 获取实际音频格式
    IMFMediaType* pActualType = nullptr;
    hr = s_Reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &pActualType);
    if (SUCCEEDED(hr))
    {
        UINT32 cbSize = 0;
        WAVEFORMATEX* pwfx = nullptr;
        MFCreateWaveFormatExFromMFMediaType(pActualType, &pwfx, &cbSize);
        if (pwfx)
        {
            s_WaveFmt = *pwfx;
            CoTaskMemFree(pwfx);
        }
        pActualType->Release();
    }
    else
    {
        // 回退：默认格式
        s_WaveFmt.wFormatTag      = WAVE_FORMAT_PCM;
        s_WaveFmt.nChannels       = 2;
        s_WaveFmt.nSamplesPerSec  = 44100;
        s_WaveFmt.wBitsPerSample  = 16;
        s_WaveFmt.nBlockAlign     = 4;
        s_WaveFmt.nAvgBytesPerSec = 44100 * 4;
        s_WaveFmt.cbSize          = 0;
    }

    // 创建 XAudio2 SourceVoice
    hr = s_XAudio2->CreateSourceVoice(&s_SourceVoice, &s_WaveFmt, 0,
        XAUDIO2_DEFAULT_FREQ_RATIO, nullptr, nullptr, nullptr);
    if (FAILED(hr))
    {
        LogSystem::Add(LOG_ERROR, "CreateSourceVoice 失败: 0x%08X", hr);
        s_Reader->Release();
        s_Reader = nullptr;
        return false;
    }

    s_SourceVoice->SetVolume(s_Volume);

    // 创建唤醒事件
    s_WakeEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);

    // 预分配缓冲池
    UINT32 bufSize = s_WaveFmt.nAvgBytesPerSec / 10; // 100ms 缓冲
    for (auto& buf : s_Buffers)
        buf.data.resize(bufSize * 2);

    s_Open = true;
    s_Eof = false;
    s_ExitThread = false;
    s_Position = 0.0;
    s_SeekTarget = -1.0;

    // 启动音频馈送线程
    s_Thread = std::thread([]() {
        while (!s_ExitThread)
        {
            if (s_Playing && !s_Eof)
                FeedAudio();
            else
                WaitForSingleObject(s_WakeEvent, 50);
        }
    });

    LogSystem::Add(LOG_INFO, "音频已打开: %dHz %dch %dbit",
        s_WaveFmt.nSamplesPerSec, s_WaveFmt.nChannels, s_WaveFmt.wBitsPerSample);
    return true;
}

// =====================================================
// 向 XAudio2 提交音频数据
// =====================================================
static void FeedAudio()
{
    if (!s_Reader || !s_SourceVoice || !s_Playing) return;

    // 处理 seek
    double seekTo = s_SeekTarget.exchange(-1.0);
    if (seekTo >= 0.0)
    {
        s_SourceVoice->FlushSourceBuffers();
        PROPVARIANT var;
        PropVariantInit(&var);
        var.vt = VT_I8;
        var.hVal.QuadPart = (LONGLONG)(seekTo * 10000000.0); // 100ns units
        s_Reader->Flush(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
        s_Reader->SetCurrentPosition(GUID_NULL, var);
        PropVariantClear(&var);
    }

    // 检查 XAudio2 缓冲队列，控制提交速率
    XAUDIO2_VOICE_STATE state;
    s_SourceVoice->GetState(&state);
    if (state.BuffersQueued >= 3) return; // 已经有足够缓冲

    // 读取音频样本
    AudioBuffer& buf = s_Buffers[s_BufIdx];
    s_BufIdx = (s_BufIdx + 1) % 3;

    DWORD flags = 0;
    DWORD dwStreamIndex = 0;
    IMFSample* pSample = nullptr;

    HRESULT hr = s_Reader->ReadSample(
        MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0,
        &dwStreamIndex, &flags, nullptr, &pSample);

    if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM))
    {
        s_Eof = true;
        if (pSample) pSample->Release();
        return;
    }

    if (!pSample) return;

    // 提取 PCM 数据
    IMFMediaBuffer* pBuffer = nullptr;
    hr = pSample->ConvertToContiguousBuffer(&pBuffer);
    if (SUCCEEDED(hr))
    {
        BYTE* pData = nullptr;
        DWORD dataLen = 0;
        hr = pBuffer->Lock(&pData, nullptr, &dataLen);
        if (SUCCEEDED(hr))
        {
            if (dataLen > buf.data.size())
                buf.data.resize(dataLen);

            memcpy(buf.data.data(), pData, dataLen);
            buf.xaBuf.Flags = 0;
            buf.xaBuf.AudioBytes = dataLen;
            buf.xaBuf.pAudioData = buf.data.data();
            buf.xaBuf.PlayBegin = 0;
            buf.xaBuf.PlayLength = 0;
            buf.xaBuf.LoopBegin = 0;
            buf.xaBuf.LoopLength = 0;
            buf.xaBuf.LoopCount = 0;
            buf.xaBuf.pContext = nullptr;

            s_SourceVoice->SubmitSourceBuffer(&buf.xaBuf);

            // 更新位置（估算）
            double bytesPerSec = (double)s_WaveFmt.nAvgBytesPerSec;
            if (bytesPerSec > 0)
                s_Position = s_Position + (double)dataLen / bytesPerSec;

            pBuffer->Unlock();
        }
        pBuffer->Release();
    }
    pSample->Release();
}

// =====================================================
// 播放控制
// =====================================================
void Play()
{
    if (!s_Open || !s_SourceVoice) return;
    s_SourceVoice->Start(0);
    s_Playing = true;
    SetEvent(s_WakeEvent);
}

void Pause()
{
    s_Playing = false;
    if (s_SourceVoice)
        s_SourceVoice->Stop(0);
}

void TogglePlay()
{
    if (s_Playing) Pause();
    else           Play();
}

void Stop()
{
    Pause();
    Seek(0.0);
}

void SetVolume(float vol)
{
    s_Volume = std::clamp(vol, 0.0f, 1.0f);
    if (s_SourceVoice)
        s_SourceVoice->SetVolume(s_Volume);
}

void Seek(double seconds)
{
    s_SeekTarget = seconds;
    s_Position = seconds;
    s_Eof = false;
}

bool IsOpen()    { return s_Open; }
bool IsPlaying() { return s_Playing; }

double GetPositionSec()
{
    return s_Position.load();
}

void SetRate(float rate)
{
    s_Rate = std::clamp(rate, 0.25f, 4.0f);
    if (s_SourceVoice)
        s_SourceVoice->SetFrequencyRatio(s_Rate);
}

// =====================================================
// 关闭
// =====================================================
void Close()
{
    s_ExitThread = true;
    SetEvent(s_WakeEvent);

    if (s_Thread.joinable())
        s_Thread.join();

    if (s_SourceVoice)
    {
        s_SourceVoice->Stop(0);
        s_SourceVoice->DestroyVoice();
        s_SourceVoice = nullptr;
    }

    if (s_Reader)
    {
        s_Reader->Release();
        s_Reader = nullptr;
    }

    if (s_WakeEvent)
    {
        CloseHandle(s_WakeEvent);
        s_WakeEvent = nullptr;
    }

    s_Open = false;
    s_Playing = false;
    s_Eof = false;
    s_Position = 0.0;
}

} // namespace AudioPlayer
