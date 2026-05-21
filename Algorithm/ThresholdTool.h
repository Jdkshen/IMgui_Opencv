#pragma once

#include <opencv2/opencv.hpp>

// =========================
// 全局变量声明（外部引用）
// =========================

extern bool g_ShowThresholdWindow;      // 阈值调试窗口显示标志
extern int gThresholdValue;             // 当前阈值
extern bool gThresholdBinaryInv;        // 是否反色

extern cv::Mat gImage;                  // 当前处理图像
extern cv::Mat gThresholdMat;           // 阈值处理结果

extern bool gNeedUpload;                // 是否需要上传到GPU

extern cv::Mat gPendingUpload;          // 待上传的RGBA图像
extern cv::Mat gOriginalImage;          // 原始图像（彩色）

// =========================
// DX12延迟释放队列（外部引用）
// =========================
struct ID3D12Resource;
extern std::vector<ID3D12Resource*> gPendingReleaseTextures;

// =========================
// 阈值工具命名空间
// =========================
namespace ThresholdTool
{
    void ShowThresholdWindow();  // 显示阈值调试窗口
    void ApplyThreshold();       // 执行阈值处理
    void ResetParams();          // 重置所有参数
}

// =========================
// 管线状态结构体
// =========================
struct PipelineState
{
    bool enableBlur = false;        // 是否启用模糊
    bool enableThreshold = false;   // 是否启用二值化
    bool enableCanny = false;       // 是否启用Canny边缘检测

    int blurSize = 5;               // 模糊核大小
    int threshold = 128;            // 二值化阈值
    int cannyLow = 50;              // Canny低阈值
    int cannyHigh = 150;            // Canny高阈值
};
