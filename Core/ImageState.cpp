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

// 同步内部状态到全局 VisionContext（兼容旧代码的全局变量访问）
void SyncLegacyAndContext()
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

    cv::Mat& OriginalRef()
    {
        if (!s_original.empty())
        {
            s_original = s_original.clone();
            gContext.originalImage = s_original;
        }
        return s_original;
    }

    cv::Mat& PendingUploadRef()
    {
        return s_pendingUpload;
    }

    bool& NeedUploadRef()
    {
        return s_needUpload;
    }

    int& WidthRef()
    {
        return s_width;
    }

    int& HeightRef()
    {
        return s_height;
    }

    int& VersionRef()
    {
        return s_version;
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
        SyncLegacyAndContext();
    }

    void SetDebugImage(const cv::Mat& image)
    {
        if (image.empty())
            return;

        s_current = image.clone();
        s_width = image.cols;
        s_height = image.rows;
        ++s_version;
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
