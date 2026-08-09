#include "GraphicsBackend.h"

#include "DX11Context.h"
#include "DX12Backend.h"
#include "../Log/LogSystem.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <pdh.h>
#include <pdhmsg.h>
#include <chrono>
#include <vector>

#pragma comment(lib, "pdh.lib")

namespace
{
std::unique_ptr<IRenderBackend> s_backend;
RenderTextureHandle s_mainTexture = kInvalidRenderTexture;
std::string s_dx12FailureReason;
std::string s_lastError;
PDH_HQUERY s_gpuQuery = nullptr;
PDH_HCOUNTER s_gpuCounter = nullptr;
double s_gpuUtilization = -1.0;
std::chrono::steady_clock::time_point s_lastGpuSample;

void ShutdownGpuMonitor()
{
    if (s_gpuQuery)
        PdhCloseQuery(s_gpuQuery);
    s_gpuQuery = nullptr;
    s_gpuCounter = nullptr;
    s_gpuUtilization = -1.0;
}

void InitializeGpuMonitor()
{
    ShutdownGpuMonitor();
    if (PdhOpenQueryW(nullptr, 0, &s_gpuQuery) != ERROR_SUCCESS ||
        PdhAddEnglishCounterW(s_gpuQuery,
            L"\\GPU Engine(*)\\Utilization Percentage", 0,
            &s_gpuCounter) != ERROR_SUCCESS)
    {
        ShutdownGpuMonitor();
        return;
    }
    PdhCollectQueryData(s_gpuQuery);
    s_lastGpuSample = std::chrono::steady_clock::now();
}

bool ForceDX11ForDiagnostics()
{
    char value[32]{};
    const DWORD length = GetEnvironmentVariableA(
        "IMGUI_OPENCV_RENDER_BACKEND", value, static_cast<DWORD>(std::size(value)));
    if (length == 0 || length >= std::size(value))
        return false;
    std::string normalized(value, length);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return normalized == "dx11" || normalized == "directx11";
}

cv::Mat NormalizeRgba(const cv::Mat& source)
{
    if (source.empty())
        return {};

    cv::Mat image = source;
    cv::Mat convertedDepth;
    if (image.depth() != CV_8U)
    {
        image.convertTo(convertedDepth, CV_8U);
        image = convertedDepth;
    }

    cv::Mat rgba;
    switch (image.channels())
    {
    case 1:
        cv::cvtColor(image, rgba, cv::COLOR_GRAY2RGBA);
        break;
    case 3:
        cv::cvtColor(image, rgba, cv::COLOR_BGR2RGBA);
        break;
    case 4:
        rgba = image;
        break;
    default:
        return {};
    }
    if (!rgba.isContinuous())
        rgba = rgba.clone();
    return rgba;
}
}

namespace GraphicsBackend
{
bool Initialize(HWND window)
{
    Shutdown();
    s_dx12FailureReason.clear();
    s_lastError.clear();

    const bool forceDX11 = ForceDX11ForDiagnostics();
    if (!forceDX11)
    {
        auto dx12 = std::make_unique<DX12Backend>();
        if (dx12->Initialize(window, s_dx12FailureReason))
        {
            s_backend = std::move(dx12);
            LogSystem::Add(LOG_INFO, "渲染后端已选择: DirectX 12");
            InitializeGpuMonitor();
            return true;
        }
        LogSystem::Add(LOG_WARN, "DirectX 12 初始化失败: %s",
            s_dx12FailureReason.c_str());
        LogSystem::Add(LOG_INFO, "正在自动回退到 DirectX 11");
    }
    else
    {
        LogSystem::Add(LOG_WARN,
            "检测到 IMGUI_OPENCV_RENDER_BACKEND=dx11，跳过 DX12 用于回退路径诊断");
    }

    auto dx11 = std::make_unique<DX11Context>();
    std::string dx11Error;
    if (dx11->Initialize(window, dx11Error))
    {
        s_backend = std::move(dx11);
        LogSystem::Add(LOG_INFO, "DirectX 11 回退初始化成功");
        LogSystem::Add(LOG_INFO, "渲染后端已选择: DirectX 11");
        InitializeGpuMonitor();
        return true;
    }

    s_lastError = "DirectX 11 fallback failed: " + dx11Error;
    if (!s_dx12FailureReason.empty())
        s_lastError = "DirectX 12 failed: " + s_dx12FailureReason + "; " + s_lastError;
    LogSystem::Add(LOG_ERROR, "DirectX 11 回退初始化失败: %s", dx11Error.c_str());
    return false;
}

void Shutdown()
{
    ShutdownGpuMonitor();
    if (s_backend)
    {
        if (s_mainTexture != kInvalidRenderTexture)
            s_backend->ReleaseTexture(s_mainTexture);
        s_backend->WaitIdle();
        s_backend->Shutdown();
    }
    s_backend.reset();
    s_mainTexture = kInvalidRenderTexture;
}

bool IsInitialized()
{
    return static_cast<bool>(s_backend);
}

RenderBackendKind Kind()
{
    return s_backend ? s_backend->Kind() : RenderBackendKind::None;
}

const char* Name()
{
    return s_backend ? s_backend->Name() : "未初始化";
}

void NewFrame()
{
    if (s_backend)
        s_backend->NewFrame();
}

bool IsOccluded()
{
    return s_backend && s_backend->IsOccluded();
}

bool Resize(unsigned int width, unsigned int height)
{
    if (!s_backend)
        return false;
    if (s_backend->Resize(width, height, s_lastError))
        return true;
    LogSystem::Add(LOG_ERROR, "%s 窗口缩放失败: %s", s_backend->Name(),
        s_lastError.c_str());
    return false;
}

bool RenderAndPresent(const ImVec4& clearColor, const ImGuiIO& io)
{
    if (!s_backend)
        return false;
    if (s_backend->RenderAndPresent(clearColor, io, s_lastError))
        return true;
    LogSystem::Add(LOG_ERROR, "%s 渲染失败: %s", s_backend->Name(),
        s_lastError.c_str());
    return false;
}

void WaitIdle()
{
    if (s_backend)
        s_backend->WaitIdle();
}

RenderTextureHandle CreateTexture()
{
    return s_backend ? s_backend->CreateTexture() : kInvalidRenderTexture;
}

bool UploadTexture(RenderTextureHandle texture, const cv::Mat& image)
{
    if (!s_backend || texture == kInvalidRenderTexture)
        return false;
    const cv::Mat rgba = NormalizeRgba(image);
    if (rgba.empty())
    {
        s_lastError = "texture upload source could not be converted to RGBA";
        LogSystem::Add(LOG_ERROR, "%s", s_lastError.c_str());
        return false;
    }
    if (s_backend->UploadTexture(texture, rgba, s_lastError))
        return true;
    LogSystem::Add(LOG_ERROR, "%s 纹理上传排队失败: %s", s_backend->Name(),
        s_lastError.c_str());
    return false;
}

void ReleaseTexture(RenderTextureHandle texture)
{
    if (s_backend && texture != kInvalidRenderTexture)
        s_backend->ReleaseTexture(texture);
}

ImTextureID TextureId(RenderTextureHandle texture)
{
    return s_backend ? s_backend->TextureId(texture) : ImTextureID_Invalid;
}

bool TextureReady(RenderTextureHandle texture)
{
    return s_backend && s_backend->TextureReady(texture);
}

bool UploadMainTexture(const cv::Mat& rgba)
{
    if (!s_backend)
        return false;
    if (s_mainTexture == kInvalidRenderTexture)
        s_mainTexture = s_backend->CreateTexture();
    return UploadTexture(s_mainTexture, rgba);
}

void ReleaseMainTexture()
{
    if (s_backend && s_mainTexture != kInvalidRenderTexture)
        s_backend->ReleaseTexture(s_mainTexture);
    s_mainTexture = kInvalidRenderTexture;
}

ImTextureID MainTextureId()
{
    return TextureId(s_mainTexture);
}

bool HasMainTexture()
{
    return TextureReady(s_mainTexture);
}

RenderMemoryInfo MemoryInfo()
{
    RenderMemoryInfo info;
    if (s_backend)
        s_backend->QueryMemoryInfo(info);
    return info;
}

double ProcessGpuUtilization()
{
    if (!s_gpuQuery || !s_gpuCounter)
        return -1.0;
    const auto now = std::chrono::steady_clock::now();
    if (now - s_lastGpuSample < std::chrono::milliseconds(500))
        return s_gpuUtilization;
    s_lastGpuSample = now;
    if (PdhCollectQueryData(s_gpuQuery) != ERROR_SUCCESS)
        return s_gpuUtilization;
    DWORD bufferSize = 0;
    DWORD itemCount = 0;
    PDH_STATUS status = PdhGetFormattedCounterArrayW(s_gpuCounter, PDH_FMT_DOUBLE,
        &bufferSize, &itemCount, nullptr);
    if (status != PDH_MORE_DATA || bufferSize == 0)
        return s_gpuUtilization;
    std::vector<unsigned char> buffer(bufferSize);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
    status = PdhGetFormattedCounterArrayW(s_gpuCounter, PDH_FMT_DOUBLE,
        &bufferSize, &itemCount, items);
    if (status != ERROR_SUCCESS)
        return s_gpuUtilization;
    const std::wstring pidToken = L"pid_" + std::to_wstring(GetCurrentProcessId()) + L"_";
    double total = 0.0;
    for (DWORD index = 0; index < itemCount; ++index)
    {
        const std::wstring name = items[index].szName ? items[index].szName : L"";
        if (name.find(pidToken) != std::wstring::npos &&
            items[index].FmtValue.CStatus == ERROR_SUCCESS)
            total += items[index].FmtValue.doubleValue;
    }
    s_gpuUtilization = std::clamp(total, 0.0, 100.0);
    return s_gpuUtilization;
}

const std::string& DX12FailureReason()
{
    return s_dx12FailureReason;
}

const std::string& LastError()
{
    return s_lastError;
}
}
