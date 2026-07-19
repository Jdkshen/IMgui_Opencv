#include "ImageLoadController.h"

#include "AsyncImageLoader.h"
#include "DX12Context.h"
#include "FrameNavigation.h"
#include "FrameSourceState.h"
#include "ImageUtils.h"
#include "OpenCVTest.h"
#include "ROIState.h"
#include "TemplateState.h"
#include "VideoCapture.h"
#include "../Log/LogSystem.h"

#include <exception>
#include <utility>

// =====================================================
// 内部状态
// =====================================================
namespace
{
std::string s_UploadRequest;     // 当前待加载的图片路径
std::string s_LastError;
bool s_RequestLoadImage = false; // 是否需要发起异步加载请求
}

// =====================================================
// ImageLoadController::Update
// 每帧调度流程：
//   1. 消费 FrameNavigation 或外部模块提交的路径
//   2. 关闭视频源并发起 AsyncImageLoader::RequestLoad
//   3. 轮询结果，成功后设置 FrameSourceState 并上传 GPU
// =====================================================
namespace ImageLoadController
{
void RequestLoad(std::string path)
{
    if (path.empty())
        return;
    VideoCapture::Close();
    s_UploadRequest = std::move(path);
    s_RequestLoadImage = true;
}

void Update()
{
    std::string navigationPath;
    if (FrameNavigation::ConsumePendingImagePath(navigationPath))
        RequestLoad(std::move(navigationPath));

    // 2. 发起异步加载请求
    if (s_RequestLoadImage && !s_UploadRequest.empty())
    {
        s_RequestLoadImage = false;
        AsyncImageLoader::RequestLoad(s_UploadRequest);
    }

    // 3. 轮询异步加载结果 → 完成时回调处理
    AsyncImageLoader::CheckAndProcess([](cv::Mat img)
    {
        try
        {
            // 释放旧纹理资源（延迟释放队列）
            FlushPendingRelease();
            if (gTexture)
            {
                gPendingReleaseTextures.push_back(gTexture);
                gTexture = nullptr;
            }

            // 判断帧源类型（单图片 vs 图片序列）
            FrameSourceType sourceType = FrameSourceType::SingleImage;
            int frameIndex = -1;
            if (FrameNavigation::IsCurrentImage(s_UploadRequest))
            {
                sourceType = FrameSourceType::ImageSequence;
                frameIndex = FrameNavigation::CurrentImageIndex();
            }
            FrameSourceState::SetCurrentFrame(img, sourceType, s_UploadRequest, frameIndex, 0.0);

            // 转换为 RGBA → 上传到 GPU 纹理
            cv::Mat rgba;
            SafeConvertToRGBA(img, rgba);
            UploadToDX12(g_pd3dDevice, g_pd3dCommandList, &gTexture, rgba,
                DXGI_FORMAT_R8G8B8A8_UNORM, gSrvCpuHandle);

            LogSystem::Add(LOG_INFO, "异步图片加载完成: %dx%d", img.cols, img.rows);
            s_LastError.clear();

            // 后处理：适配窗口、清空交互状态、清空模板匹配
            FrameNavigation::FitImageToWindow();
            ROIState::ClearInteraction();
TemplateState::ClearResults();
        }
        catch (const std::exception& e)
        {
            s_LastError = std::string("图片加载后处理异常: ") + e.what();
            LogSystem::Add(LOG_ERROR, "%s", s_LastError.c_str());
        }
        catch (...)
        {
            s_LastError = "图片加载后处理发生未知异常";
            LogSystem::Add(LOG_ERROR, "%s", s_LastError.c_str());
        }
    }, [](const std::string& error)
    {
        s_LastError = error;
    });
}

bool ConsumeLastError(std::string& error)
{
    if (s_LastError.empty())
        return false;
    error = std::move(s_LastError);
    s_LastError.clear();
    return true;
}
}
