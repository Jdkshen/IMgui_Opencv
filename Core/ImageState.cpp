#include "ImageState.h"

#include "ImageUtils.h"
#include "VisionContext.h"

#include <algorithm>

extern cv::Mat gImage;
extern cv::Mat gOriginalImage;
extern cv::Mat gPendingUpload;
extern bool gNeedUpload;
extern int gImageWidth;
extern int gImageHeight;
extern int g_ImageVersion;

namespace
{
cv::Mat s_current;
cv::Mat s_original;
int s_width = 0;
int s_height = 0;
int s_version = 0;

void SyncLegacyAndContext()
{
    gImage = s_current.clone();
    gOriginalImage = s_original.clone();
    gImageWidth = s_width;
    gImageHeight = s_height;
    g_ImageVersion = s_version;

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
        s_version = std::max(s_version, g_ImageVersion) + 1;
        SyncLegacyAndContext();
    }

    void SetDebugImage(const cv::Mat& image)
    {
        if (image.empty())
            return;

        s_current = image.clone();
        s_width = image.cols;
        s_height = image.rows;
        s_version = std::max(s_version, g_ImageVersion) + 1;

        gImage = s_current.clone();
        gImageWidth = s_width;
        gImageHeight = s_height;
        g_ImageVersion = s_version;
        gContext.image = s_current.clone();
        gContext.width = s_width;
        gContext.height = s_height;
        gContext.imageVersion = s_version;

        cv::Mat rgba;
        SafeConvertToRGBA(s_current, rgba);
        if (!rgba.empty())
        {
            gPendingUpload = rgba;
            gNeedUpload = true;
        }
    }

    void Clear()
    {
        s_current.release();
        s_original.release();
        s_width = 0;
        s_height = 0;
        s_version = 0;

        gImage.release();
        gOriginalImage.release();
        gPendingUpload.release();
        gNeedUpload = false;
        gImageWidth = 0;
        gImageHeight = 0;
        g_ImageVersion = 0;

        gContext.image.release();
        gContext.originalImage.release();
        gContext.width = 0;
        gContext.height = 0;
        gContext.imageVersion = 0;
    }
}
