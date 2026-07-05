#pragma once

#include <opencv2/core/mat.hpp>

// =====================================================
// ImageState — 统一图像状态管理
// 集中管理当前处理图像的所有状态，替代散落的全局变量
// 提供只读访问（const 引用）和可写引用（供内部管线使用）
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

    // ---- 可写引用（供内部模块使用，避免不必要的拷贝） ----
    cv::Mat& CurrentRef();          // 当前图像可写引用（处理管线修改此图像）
    cv::Mat& OriginalRef();         // 原始图像可写引用
    cv::Mat& PendingUploadRef();    // GPU 上传缓冲区引用
    bool& NeedUploadRef();          // GPU 上传标记引用
    int& WidthRef();                // 宽度可写引用
    int& HeightRef();               // 高度可写引用
    int& VersionRef();              // 版本号可写引用

    // ---- 操作 ----
    void SetImage(const cv::Mat& image);     // 设置新图像（更新原始+当前，递增版本号）
    void SetDebugImage(const cv::Mat& image); // 设置调试图像（只更新当前，保留原始）
    void Clear();                            // 清空所有图像状态
}
