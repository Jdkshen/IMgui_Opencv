#include "../Windows_imgui.h"
#include"../Core/DX12Context.h"

// =========================
// 阈值调试窗口：全局状态变量
// =========================

bool g_ShowThresholdWindow = false;         // 窗口显示标志

bool gNeedUpload = false;                   // 是否需要上传处理结果到GPU

cv::Mat gOriginalImage;                     // 原始图像（彩色，永久保留）
cv::Mat gImage;                             // 当前处理的输入图像
cv::Mat gThresholdMat;                      // 阈值处理结果
cv::Mat gPendingUpload;                     // 待上传到GPU的RGBA图像

bool gUseGray = false;                      // 是否先转为灰度处理

// =========================
// 图像处理参数
// =========================
int gThresholdValue = 128;                  // 二值化阈值
bool gThresholdBinaryInv = false;           // 是否反色

int gBlurSize = 1;                          // 高斯模糊核大小

int gCannyLow = 50;                         // Canny低阈值
int gCannyHigh = 150;                       // Canny高阈值

float gBrightness = 0.0f;                   // 亮度调整
float gContrast = 1.0f;                     // 对比度调整

int gProcessMode = 0;                       // 处理模式

// =========================
// 性能计时（单位：毫秒）
// =========================
float gTimeGray = 0.0f;     // 灰度转换耗时
float gTimeBlur = 0.0f;     // 模糊耗时
float gTimeFilter = 0.0f;   // 滤波耗时
float gTimeRGBA = 0.0f;     // RGBA转换耗时
float gTimeTotal = 0.0f;    // 总耗时

// =========================
// 图像处理管线
// =========================
using ImgOp = std::function<cv::Mat(const cv::Mat&)>;
std::vector<ImgOp> gPipeline;

PipelineState gPipe;                         // 管线状态配置

// =========================
// 图像处理算子
// =========================
namespace Ops
{
    // 高斯模糊
    cv::Mat Blur(const cv::Mat& img, int k)
    {
        cv::Mat out;
        k = k * 2 + 1;  // 确保核大小为奇数
        if (k < 3) k = 3;
        cv::GaussianBlur(img, out, cv::Size(k, k), 0);
        return out;
    }

    // 二值化阈值
    cv::Mat Threshold(const cv::Mat& img, int t)
    {
        cv::Mat gray, out;
        if (img.channels() == 3)
            cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
        else
            gray = img;
        cv::threshold(gray, out, t, 255, cv::THRESH_BINARY);
        return out;
    }

    // Canny边缘检测
    cv::Mat Canny(const cv::Mat& img, int low, int high)
    {
        cv::Mat gray, out;
        if (img.channels() == 3)
            cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
        else
            gray = img;
        cv::Canny(gray, out, low, high);
        return out;
    }
}

// =========================
// 阈值工具命名空间
// =========================
namespace ThresholdTool
{
    // =========================
    // 执行图像处理管线
    // =========================
    void ApplyProcess()
    {
        if (gImage.empty()) return;

        using clock = std::chrono::high_resolution_clock;
        auto t0 = clock::now();

        cv::Mat src = gImage;
        cv::Mat input;

        // 阶段1：灰度转换
        auto t1 = clock::now();
        if (gUseGray)
        {
            if (src.channels() == 4)
                cv::cvtColor(src, input, cv::COLOR_BGRA2GRAY);
            else if (src.channels() == 3)
                cv::cvtColor(src, input, cv::COLOR_BGR2GRAY);
            else
                input = src;
        }
        else
        {
            input = src;
        }
        auto t2 = clock::now();

        cv::Mat result = input;

        // 阶段2：高斯模糊
        if (gPipe.enableBlur)
            result = Ops::Blur(result, gPipe.blurSize);
        auto t3 = clock::now();

        // 阶段3：Canny边缘 / 二值化（互斥）
        if (gPipe.enableThreshold && gPipe.enableCanny)
            result = Ops::Canny(result, gPipe.cannyLow, gPipe.cannyHigh);
        else if (gPipe.enableCanny)
            result = Ops::Canny(result, gPipe.cannyLow, gPipe.cannyHigh);
        else if (gPipe.enableThreshold)
            result = Ops::Threshold(result, gPipe.threshold);
        auto t4 = clock::now();

        // 阶段4：转RGBA → 标记GPU上传
        cv::Mat rgba;
        if (result.channels() == 1)
            cv::cvtColor(result, rgba, cv::COLOR_GRAY2RGBA);
        else if (result.channels() == 3)
            cv::cvtColor(result, rgba, cv::COLOR_BGR2RGBA);
        else
            cv::cvtColor(result, rgba, cv::COLOR_BGRA2RGBA);
        auto t5 = clock::now();

        // 标记需要上传到GPU
        gPendingUpload = rgba;
        gNeedUpload = true;
        auto t6 = clock::now();

        // 阶段5：记录各步骤耗时
        auto ms = [](auto a, auto b)
        {
            return std::chrono::duration<float, std::milli>(b - a).count();
        };
        gTimeGray = ms(t0, t2);
        gTimeBlur = ms(t2, t3);
        gTimeFilter = ms(t3, t4);
        gTimeRGBA = ms(t4, t5);
        gTimeTotal = ms(t0, t6);
    }

    // =========================
    // 显示阈值调试窗口
    // =========================
    void ShowThresholdWindow()
    {
        if (!g_ShowThresholdWindow) return;

        if (ImGui::Begin("图像处理", &g_ShowThresholdWindow)) //,ImGuiWindowFlags_NoDocking  禁止停靠窗口
        {
            // =========================
            // 重置按钮 + 灰度复选框
            // =========================
            bool changed = false;

            if (ImGui::Button("重置参数"))
            {
                gPipeline.clear();
                ResetParams();
                ApplyProcess();
            }
            ImGui::SameLine();
            changed |= ImGui::Checkbox("使用灰度", &gUseGray);

            ImGui::Separator();

            // =========================
            // 参数变更检测（任一参数变化 => 重新处理）
            // =========================

            ImGui::Text("图像模糊处理（高斯滤波）");
            changed |= ImGui::Checkbox("启用模糊", &gPipe.enableBlur);
            changed |= ImGui::SliderInt("核大小", &gPipe.blurSize, 1, 20);

            ImGui::Separator();

            ImGui::Text("图像二值化（阈值分割）");
            changed |= ImGui::Checkbox("启用二值化", &gPipe.enableThreshold);
            changed |= ImGui::SliderInt("阈值", &gPipe.threshold, 0, 255);

            ImGui::Separator();

            ImGui::Text("Canny边缘检测");
            changed |= ImGui::Checkbox("启用Canny", &gPipe.enableCanny);
            changed |= ImGui::SliderInt("低阈值", &gPipe.cannyLow, 0, 255);
            changed |= ImGui::SliderInt("高阈值", &gPipe.cannyHigh, 0, 255);

            // 防抖：拖动滑块时等3帧再执行，避免每帧都跑完整管线
            static int debounceFrames = 0;
            if (changed)
                debounceFrames = 3;
            if (debounceFrames > 0)
            {
                debounceFrames--;
                if (debounceFrames == 0)
                    ApplyProcess();
            }

            // =========================
            // 显示当前参数值
            // =========================
            ImGui::Separator();
            ImGui::Text("当前参数: 灰度=%s, 模糊核=%d, 阈值=%d, Canny=(%d,%d)",
                gUseGray ? "是" : "否",
                gPipe.blurSize,
                gPipe.threshold,
                gPipe.cannyLow, gPipe.cannyHigh);

            // =========================
            // 性能分析
            // =========================
            ImGui::Separator();
            ImGui::Text("性能分析（单位: 毫秒）");
            ImGui::Text("灰度转换 : %.3f ms", gTimeGray);
            ImGui::Text("模糊处理 : %.3f ms", gTimeBlur);
            ImGui::Text("滤波处理 : %.3f ms", gTimeFilter);
            ImGui::Text("RGBA转换 : %.3f ms", gTimeRGBA);
            ImGui::Separator();
            ImGui::Text("总耗时   : %.3f ms", gTimeTotal);
        }
        ImGui::End();
    }

    // =========================
    // 重置所有处理参数到默认值
    // =========================
    void ResetParams()
    {
        gUseGray = false;

        gPipe.enableBlur = false;
        gPipe.blurSize = 5;

        gPipe.enableThreshold = false;
        gPipe.threshold = 128;

        gPipe.enableCanny = false;
        gPipe.cannyLow = 50;
        gPipe.cannyHigh = 150;

        gThresholdValue = 128;
        gBlurSize = 5;
        gCannyLow = 50;
        gCannyHigh = 150;
    }
}