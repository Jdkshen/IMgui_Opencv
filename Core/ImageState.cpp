#include "ImageState.h"

#include "ImageUtils.h"
#include "VisionContext.h"

namespace
{
cv::Mat s_current;
cv::Mat s_original;
cv::Mat s_pendingUpload;
bool s_needUpload = false;
int s_width = 0;
int s_height = 0;
int s_version = 0;

void SyncLegacyAndContext()
{
    gContext.image = s_current.clone();
    gContext.originalImage = s_original.clone();
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
        return s_current;
    }

    cv::Mat& OriginalRef()
    {
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
        gContext.image = s_current.clone();
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
