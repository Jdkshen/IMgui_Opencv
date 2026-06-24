#include "ImageLoadController.h"

#include "AsyncImageLoader.h"
#include "DX12Context.h"
#include "FrameNavigation.h"
#include "FrameSourceState.h"
#include "ImageUtils.h"
#include "LegacyAppState.h"
#include "OpenCVTest.h"
#include "ROIState.h"
#include "UIStateBridge.h"
#include "VideoCapture.h"
#include "../Algorithm/TemplateMatch.h"
#include "../Log/LogSystem.h"

namespace
{
std::string s_UploadRequest;
bool s_RequestLoadImage = false;
}

namespace ImageLoadController
{
void Update()
{
    if (!pendingPath.empty())
    {
        VideoCapture::Close();
        s_UploadRequest = pendingPath;
        pendingPath.clear();
        s_RequestLoadImage = true;
    }

    if (s_RequestLoadImage && !s_UploadRequest.empty())
    {
        s_RequestLoadImage = false;
        AsyncImageLoader::RequestLoad(s_UploadRequest);
    }

    AsyncImageLoader::CheckAndProcess([](cv::Mat img)
    {
        try
        {
            FlushPendingRelease();
            if (gTexture)
            {
                gPendingReleaseTextures.push_back(gTexture);
                gTexture = nullptr;
            }

            FrameSourceType sourceType = FrameSourceType::SingleImage;
            int frameIndex = -1;
            if (FrameNavigation::IsCurrentImage(s_UploadRequest))
            {
                sourceType = FrameSourceType::ImageSequence;
                frameIndex = FrameNavigation::CurrentImageIndex();
            }
            FrameSourceState::SetCurrentFrame(img, sourceType, s_UploadRequest, frameIndex, 0.0);

            cv::Mat rgba;
            SafeConvertToRGBA(img, rgba);
            UploadToDX12(g_pd3dDevice, g_pd3dCommandList, &gTexture, rgba,
                DXGI_FORMAT_R8G8B8A8_UNORM, gSrvCpuHandle);

            LogSystem::Add(LOG_INFO, "异步图片加载完成: %dx%d", img.cols, img.rows);
            FrameNavigation::FitImageToWindow();
            ROIState::ClearInteraction();
            TemplateMatch::Clear();
        }
        catch (...)
        {
            LogSystem::Add(LOG_ERROR, "异步图片加载后处理异常");
        }
    });
}
}
