#pragma once

#include "../Algorithm/FrameSource.h"
#include <string>

namespace FrameSourceState
{
    void SetCurrentFrame(const cv::Mat& bgr,
        FrameSourceType type,
        const std::string& sourcePath = {},
        int frameIndex = -1,
        double timestampMs = 0.0);

    const FramePacket& Current();
    FrameSourceType GetType();
    bool HasFrame();
    void Clear();
}
