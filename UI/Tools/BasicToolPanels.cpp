#include "BasicToolPanels.h"

#include "../../Algorithm/ThresholdTool.h"
#include "../../include/imgui/imgui.h"

void RegisterBasicToolPanels(
    std::unordered_map<int, ToolUIFn>& panels,
    const ToolPanelContext& ui)
{
    panels[2] = [ui](ToolInstance& tool, int instanceIndex)
    {
        ui.beginCard("Blob分析");
        if (ui.primaryButton("执行Blob分析"))
            ui.runTool(instanceIndex);
        ui.drawSearchROI(tool, instanceIndex);
        ui.sectionHeader("参数");

        BlobSettings& settings = tool.blob;
        ImGui::SliderInt("最小面积", &settings.minArea, 1, 10000);
        ImGui::SliderInt("最大面积", &settings.maxArea, 100, 100000);
        const char* thresholdModes[] = {"Otsu自动阈值", "手动阈值"};
        ImGui::Combo("阈值模式", &settings.thresholdMode,
            thresholdModes, IM_ARRAYSIZE(thresholdModes));
        if (settings.thresholdMode == 1)
            ImGui::SliderInt("阈值", &settings.threshold, 0, 255);
        ImGui::Checkbox("反相", &settings.invert);

        const char* connectivityModes[] = {"4邻域", "8邻域"};
        int connectivityIndex = settings.connectivity == 4 ? 0 : 1;
        if (ImGui::Combo("连通方式", &connectivityIndex,
            connectivityModes, IM_ARRAYSIZE(connectivityModes)))
        {
            settings.connectivity = connectivityIndex == 0 ? 4 : 8;
        }
        ImGui::SliderFloat("最小圆度", &settings.minCircularity, 0.0f, 1.0f, "%.3f");
        ImGui::SliderFloat("最大圆度", &settings.maxCircularity,
            settings.minCircularity, 1.0f, "%.3f");
        ImGui::SliderFloat("最小长宽比", &settings.minAspectRatio, 0.0f, 20.0f, "%.2f");
        ImGui::SliderFloat("最大长宽比", &settings.maxAspectRatio,
            settings.minAspectRatio, 100.0f, "%.2f");
        ui.endCard();
    };

    panels[3] = [ui](ToolInstance& tool, int instanceIndex)
    {
        ui.beginCard("阈值调试");
        ThresholdSettings& settings = tool.threshold;
        const auto applyPreview = [&settings]()
        {
            PipelineState pipeline;
            pipeline.enableBlur = settings.enableBlur;
            pipeline.blurSize = settings.blurSize;
            pipeline.enableThreshold = settings.enableThreshold;
            pipeline.threshold = settings.threshold;
            pipeline.enableCanny = settings.enableCanny;
            pipeline.cannyLow = settings.cannyLow;
            pipeline.cannyHigh = settings.cannyHigh;
            ThresholdTool::ApplyProcess(settings.useGray, pipeline);
        };

        if (ui.secondaryButton("重置参数"))
        {
            settings = ThresholdSettings{};
            applyPreview();
        }
        if (ui.primaryButton("执行处理"))
            ui.runTool(instanceIndex);

        ui.drawSearchROI(tool, instanceIndex);
        ui.sectionHeader("参数");
        if (ImGui::Checkbox("转为灰度", &settings.useGray))
            applyPreview();
        if (ImGui::Checkbox("高斯模糊", &settings.enableBlur))
            applyPreview();
        if (settings.enableBlur && ImGui::SliderInt("模糊核", &settings.blurSize, 1, 10))
            applyPreview();
        if (ImGui::Checkbox("二值化", &settings.enableThreshold))
            applyPreview();
        if (settings.enableThreshold && ImGui::SliderInt("阈值", &settings.threshold, 0, 255))
            applyPreview();
        if (ImGui::Checkbox("Canny边缘", &settings.enableCanny))
            applyPreview();
        if (settings.enableCanny)
        {
            if (ImGui::SliderInt("Canny低", &settings.cannyLow, 0, 255))
                applyPreview();
            if (ImGui::SliderInt("Canny高", &settings.cannyHigh, 0, 255))
                applyPreview();
        }
        ui.endCard();
    };

    panels[8] = [ui](ToolInstance& tool, int instanceIndex)
    {
        ui.beginCard("形态学");
        MorphologySettings& settings = tool.morphology;
        if (ui.secondaryButton("重置参数"))
            settings = MorphologySettings{};
        if (ui.primaryButton("执行形态学##morph"))
            ui.runTool(instanceIndex);

        ui.drawSearchROI(tool, instanceIndex);
        ui.sectionHeader("参数");
        const char* operationNames[] = {
            "Erode 腐蚀", "Dilate 膨胀", "Open 开运算", "Close 闭运算",
            "Gradient 梯度", "TopHat 顶帽", "BlackHat 黑帽"};
        ImGui::Combo("操作##morph_op", &settings.operation, operationNames, 7);
        ImGui::SliderInt("核大小##morph_kernel_size", &settings.kernelSize, 1, 15);
        const char* kernelShapes[] = {"矩形", "椭圆", "十字"};
        ImGui::Combo("核形状##morph_kernel_shape", &settings.kernelShape, kernelShapes, 3);
        ImGui::SliderInt("迭代##morph_iterations", &settings.iterations, 1, 10);
        ImGui::Checkbox("灰度##morph", &settings.useGray);
        ui.endCard();
    };

    panels[9] = [ui](ToolInstance& tool, int instanceIndex)
    {
        ui.beginCard("颜色分析");
        ColorAnalysisSettings& settings = tool.colorAnalysis;
        if (ui.secondaryButton("重置参数"))
            settings = ColorAnalysisSettings{};
        if (ui.primaryButton("执行颜色分析##color"))
            ui.runTool(instanceIndex);

        ui.drawSearchROI(tool, instanceIndex);
        ui.sectionHeader("参数");
        const char* colorSpaces[] = {"BGR", "HSV", "Lab", "YCbCr", "Gray"};
        ImGui::Combo("色域##color", &settings.colorSpace, colorSpaces, 5);
        ImGui::SliderInt("直方图Bins##color", &settings.histogramBins, 8, 128);
        ImGui::Checkbox("显示直方图##color", &settings.showHistogram);
        if (settings.showHistogram)
            ImGui::SliderInt("高度##color", &settings.histogramHeight, 50, 300);

        ui.endCard();
    };
}
