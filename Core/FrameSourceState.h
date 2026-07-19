#pragma once

#include "../Algorithm/FrameSource.h"
#include <string>

// =====================================================
// FrameSourceState — 统一帧源状态管理
// 管理当前帧的数据包（FramePacket），统一处理图片/视频/摄像头的帧输出
// =====================================================
namespace FrameSourceState
{
    // 设置当前帧：同步更新 ImageState 和 VisionContext
    void SetCurrentFrame(const cv::Mat& bgr,
        FrameSourceType type,
        const std::string& sourcePath = {},
        int frameIndex = -1,
        double timestampMs = 0.0);

    const FramePacket& Current();   // 获取当前帧数据包
    FrameSourceType GetType();      // 获取帧源类型
    bool HasFrame();                // 是否有有效帧
    void Clear();                   // 清空帧状态
}
