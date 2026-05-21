#pragma once

#include <opencv2/opencv.hpp>

// =========================
// 管线状态结构体（需在 extern 之前定义）
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

// =========================
// 全局变量声明（外部引用）
// =========================

extern bool g_ShowThresholdWindow;      // 阈值调试窗口显示标志
extern int gThresholdValue;             // 当前阈值
extern bool gThresholdBinaryInv;        // 是否反色
extern int gCannyLow;                   // Canny低阈值
extern int gCannyHigh;                  // Canny高阈值
extern bool gUseGray;                   // 是否转为灰度处理

extern cv::Mat gImage;                  // 当前处理图像
extern cv::Mat gThresholdMat;           // 阈值处理结果

extern bool gNeedUpload;                // 是否需要上传到GPU

extern cv::Mat gPendingUpload;          // 待上传的RGBA图像
extern cv::Mat gOriginalImage;          // 原始图像（彩色）

extern float gTimeTotal;                // 处理总耗时(ms)
extern PipelineState gPipe;             // 管线状态配置

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
    void ApplyProcess();        // 执行阈值处理管线
    void ResetParams();          // 重置所有参数
}
