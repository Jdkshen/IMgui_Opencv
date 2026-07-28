#pragma once

#include <opencv2/core/mat.hpp>
#include <memory>

struct ImmutableImageFrame
{
    std::shared_ptr<const cv::Mat> current;
    std::shared_ptr<const cv::Mat> original;
    int version = 0;

    bool valid() const { return current && !current->empty(); }
};

// =====================================================
// ImageState — 统一图像状态管理
// 集中管理当前处理图像的所有状态，替代散落的全局变量
// 提供只读访问（const 引用）和受控的管线/GPU 写入入口
// =====================================================
namespace ImageState
{
    // ---- 状态查询 ----
    bool HasImage();                // 是否有图像加载
    const cv::Mat& Current();       // 当前处理图像（只读，可能是处理后的中间结果）
    const cv::Mat& Original();      // 原始图像（只读，始终是加载时的副本）
    int Width();                    // 图像宽度
    int Height();                   // 图像高度
    int Version();                  // 图像版本号（每次 SetImage 递增）
    ImmutableImageFrame AcquireImmutableFrame(); // 共享只读帧，不复制像素

    // ---- 受控的可写引用（供处理管线和 GPU 上传使用） ----
    cv::Mat& CurrentRef();          // 当前图像可写引用（处理管线修改此图像）
    cv::Mat& PendingUploadRef();    // GPU 上传缓冲区引用
    bool& NeedUploadRef();          // GPU 上传标记引用

    // ---- 操作 ----
    void SetImage(const cv::Mat& image);     // 设置新图像（更新原始+当前，递增版本号）
    void SetDebugImage(const cv::Mat& image); // 设置调试图像（只更新当前，保留原始）
    void Clear();                            // 清空所有图像状态
}
