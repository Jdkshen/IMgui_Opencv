#include "ImageState.h"

#include "ImageUtils.h"
#include "VisionContext.h"

// =====================================================
// 内部状态（模块私有，外部通过公开接口访问）
// =====================================================
namespace
{
cv::Mat s_current;         // 当前处理图像（可能被工具管线修改）
cv::Mat s_original;        // 原始图像副本（始终保持不变）
cv::Mat s_pendingUpload;   // 待上传到 GPU 的 RGBA 图像
bool s_needUpload = false; // GPU 上传标记
int s_width = 0;           // 图像宽度
int s_height = 0;          // 图像高度
int s_version = 0;         // 图像版本号（每次 SetImage 递增，用于缓存失效）

// ImageState 是所有权入口；VisionContext 保留当前帧的执行视图。
void SyncVisionContext()
{
    gContext.image = s_current;
    gContext.originalImage = s_original;
    gContext.width = s_width;
    gContext.height = s_height;
    gContext.imageVersion = s_version;
}
}

namespace ImageState
{
    bool HasImage()
    {
        return !s_current.empty();
    }

    cv::Mat& CurrentRef()
    {
        if (!s_current.empty())
        {
            s_current = s_current.clone();
            gContext.image = s_current;
        }
        return s_current;
    }

    cv::Mat& PendingUploadRef()
    {
        return s_pendingUpload;
    }

    bool& NeedUploadRef()
    {
        return s_needUpload;
    }

    const cv::Mat& Current()
    {
        return s_current;
    }

    const cv::Mat& Original()
    {
        return s_original;
    }

    int Width()
    {
        return s_width;
    }

    int Height()
    {
        return s_height;
    }

    int Version()
    {
        return s_version;
    }

    ImmutableImageFrame AcquireImmutableFrame()
    {
        ImmutableImageFrame frame;
        if (!s_current.empty())
            frame.current = std::make_shared<const cv::Mat>(s_current);
        if (!s_original.empty())
            frame.original = std::make_shared<const cv::Mat>(s_original);
        frame.version = s_version;
        return frame;
    }

    void SetImage(const cv::Mat& image)
    {
        if (image.empty())
            return;

        s_current = image.clone();
        s_original = image.clone();
        s_width = image.cols;
        s_height = image.rows;
        ++s_version;
        SyncVisionContext();
    }

    void SetDebugImage(const cv::Mat& image)
    {
        if (image.empty())
            return;

        s_current = image.clone();
        s_width = image.cols;
        s_height = image.rows;
        gContext.image = s_current;
        gContext.width = s_width;
        gContext.height = s_height;
        gContext.imageVersion = s_version;

        cv::Mat rgba;
        SafeConvertToRGBA(s_current, rgba);
        if (!rgba.empty())
        {
            s_pendingUpload = rgba;
            s_needUpload = true;
        }
    }

    void Clear()
    {
        s_current.release();
        s_original.release();
        s_width = 0;
        s_height = 0;
        s_version = 0;

        s_pendingUpload.release();
        s_needUpload = false;
        gContext.image.release();
        gContext.originalImage.release();
        gContext.width = 0;
        gContext.height = 0;
        gContext.imageVersion = 0;
    }
}
