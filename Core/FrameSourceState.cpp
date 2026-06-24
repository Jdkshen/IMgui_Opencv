#include "FrameSourceState.h"

#include "ImageState.h"
#include "VisionContext.h"

namespace
{
FramePacket s_Current;
}

namespace FrameSourceState
{
    void SetCurrentFrame(const cv::Mat& bgr,
        FrameSourceType type,
        const std::string& sourcePath,
        int frameIndex,
        double timestampMs)
    {
        if (bgr.empty())
            return;

        ImageState::SetImage(bgr);

        s_Current.original = ImageState::Original().clone();
        s_Current.sourcePath = sourcePath;
        s_Current.frameIndex = frameIndex;
        s_Current.timestampMs = timestampMs;
        s_Current.sourceType = type;
        gContext.frame = s_Current;
    }

    const FramePacket& Current()
    {
        return s_Current;
    }

    FrameSourceType GetType()
    {
        return s_Current.sourceType;
    }

    bool HasFrame()
    {
        return !s_Current.original.empty();
    }

    void Clear()
    {
        s_Current.clear();
        gContext.frame.clear();
    }
}
