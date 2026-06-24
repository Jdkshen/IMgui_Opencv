#pragma once
#include <string>
#include <vector>
#include <opencv2/core/mat.hpp>

// =====================================================
// FrameSource - 统一输入源抽象
//
// 四种输入源统一产出 FramePacket：
//   SingleImage    -> 单张图片（固定一帧）
//   ImageSequence  -> 文件夹图片序列（可迭代）
//   Video          -> 视频文件（按帧读取）
//   Camera         -> 摄像头实时流
//
// 工具只面对 FramePacket.original，不关心来源。
// 原图工具 type=12 直接返回当前帧。
// =====================================================

enum class FrameSourceType
{
    SingleImage,    // 单张图片（无迭代）
    ImageSequence,  // 文件夹图片序列
    Video,          // 视频文件
    Camera          // 摄像头实时流
};

/// 每帧数据包（不携带所有权，原始帧只读引用）
struct FramePacket
{
    cv::Mat         original;           // 原始帧（BGR，不可修改）
    std::string     sourcePath;         // 文件路径 / 摄像头索引描述
    int             frameIndex = -1;    // 帧序号（从 0 开始）
    double          timestampMs = 0;    // 时间戳（毫秒）
    FrameSourceType sourceType = FrameSourceType::SingleImage;

    bool valid() const { return !original.empty(); }
    void clear()
    {
        original = cv::Mat();
        sourcePath.clear();
        frameIndex = -1;
        timestampMs = 0;
        sourceType = FrameSourceType::SingleImage;
    }
};

// =====================================================
// 图片序列管理器（ImageSequence 模式专用）
// =====================================================
struct ImageSequence
{
    std::vector<std::string> imagePaths;  // 所有图片路径
    int currentIndex = -1;                // 当前帧索引（-1 表示无图）
    bool loopEnabled = false;             // 循环标志

    bool empty() const { return imagePaths.empty(); }
    int size() const { return (int)imagePaths.size(); }
    bool hasCurrent() const
    {
        return currentIndex >= 0 && currentIndex < (int)imagePaths.size();
    }

    const std::string& currentPath() const
    {
        static const std::string s_empty;
        return hasCurrent() ? imagePaths[currentIndex] : s_empty;
    }

    bool hasNext() const
    {
        if (imagePaths.empty()) return false;
        return loopEnabled || currentIndex + 1 < (int)imagePaths.size();
    }

    /// 前进到下一帧。循环模式下最后一张回到第 0 张。
    /// 返回 false 表示没有下一帧（非循环模式且已到末尾）。
    bool advance()
    {
        if (imagePaths.empty()) return false;
        if (currentIndex + 1 < (int)imagePaths.size())
        {
            currentIndex++;
            return true;
        }
        if (loopEnabled)
        {
            currentIndex = 0;
            return true;
        }
        return false;
    }

    /// 后退到上一帧。
    bool retreat()
    {
        if (imagePaths.empty()) return false;
        if (currentIndex > 0)
        {
            currentIndex--;
            return true;
        }
        return false;
    }

    /// 跳转到指定索引。
    bool seek(int index)
    {
        if (index < 0 || index >= (int)imagePaths.size())
            return false;
        currentIndex = index;
        return true;
    }

    void reset()
    {
        imagePaths.clear();
        currentIndex = -1;
        loopEnabled = false;
    }
};
