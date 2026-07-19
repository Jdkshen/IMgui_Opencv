#include "FrameSourceState.h"

#include "ImageState.h"
#include "VisionContext.h"

// =====================================================
// 内部状态（模块私有）
// =====================================================
namespace
{
FramePacket s_Current;  // 当前帧数据包
}

namespace FrameSourceState
{
    // 设置当前帧：同步更新 ImageState + VisionContext 全局上下文
    void SetCurrentFrame(const cv::Mat& bgr,
        FrameSourceType type,
        const std::string& sourcePath,
        int frameIndex,
        double timestampMs)
    {
        if (bgr.empty())
            return;

        ImageState::SetImage(bgr);  // 更新图像状态（触发版本号递增）

        s_Current.original = ImageState::Original().clone();
        s_Current.sourcePath = sourcePath;
        s_Current.frameIndex = frameIndex;
        s_Current.timestampMs = timestampMs;
        s_Current.sourceType = type;
        gContext.frame = s_Current;  // 同步到全局上下文
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
