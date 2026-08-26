#include "AdvancedDetectionToolPanels.h"

#include "../../Algorithm/ITool.h"
#include "../../Algorithm/MultiColorFinder.h"
#include "../../Algorithm/OpenCVYoloDetector.h"
#include "../../Algorithm/ShapeMatcher.h"
#include "../../Algorithm/YOLODetector.h"
#include "../../Core/ImageState.h"
#include "../../Core/OpenFileDialog.h"
#include "../../Core/RealtimeDetectionState.h"
#include "../../Core/TemplateState.h"
#include "../../Core/ToolAssetService.h"
#include "../../Core/ToolChainState.h"
#include "../../Core/ToolController.h"
#include "../../Core/VideoCapture.h"
#include "../../Log/LogSystem.h"
#include "../../Renderer/PreviewTextureCache.h"
#include "../../include/imgui/imgui.h"

#include <opencv2/imgproc.hpp>

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>

namespace
{
std::string FileName(const std::string& path)
{
    const std::size_t separator = path.find_last_of("\\/");
    return separator == std::string::npos ? path : path.substr(separator + 1);
}

struct AdvancedPanelUi
{
    ToolPanelContext context;

    void BeginCard(const char* title) const { context.beginCard(title); }
    void BeginCard(const char* title, const char* icon) const
    {
        context.beginCardWithIcon(title, icon);
    }
    void EndCard() const { context.endCard(); }
    void SectionHeader(const char* title) const { context.sectionHeader(title); }
    bool PrimaryButton(const char* label) const { return context.primaryButton(label); }
    bool SecondaryButton(const char* label) const
    {
        return context.secondaryButton(label);
    }
    bool SecondaryButton(const char* label, float width) const
    {
        return context.secondaryButtonSized(label, width);
    }
    void ParamLabel(const char* label, float width = 0.0f) const
    {
        context.parameterLabel(label, width);
    }
    bool RunToolFromCard(int instanceIndex) const
    {
        return context.runTool(instanceIndex);
    }
    void DrawSearchROIControls(ToolInstance& tool, int instanceIndex) const
    {
        context.drawSearchROI(tool, instanceIndex);
    }
    void MarkCurrentRecipeAssetsDirty() const
    {
        context.markRecipeAssetsDirty();
    }
};
}

void RegisterAdvancedDetectionToolPanels(
    std::unordered_map<int, ToolUIFn>& panels,
    const ToolPanelContext& context)
{
    const AdvancedPanelUi panelUi{context};

        // 1: 模板匹配
        panels[1] = [panelUi](ToolInstance &it, int inst)
        {
            panelUi.BeginCard("模板匹配");
            if (panelUi.SecondaryButton("重置参数"))
            {
                ToolAssetService::ClearAsset(it, ToolAssetKind::TemplateMatch);
                panelUi.MarkCurrentRecipeAssetsDirty();
                it.tplGray = false; it.tplBinary = false; it.tplBinThresh = 128;
                it.tplEdge = false; it.tplEdgeLow = 50; it.tplEdgeHigh = 150;
                it.imgUseGray = false; it.imgEnableThreshold = false; it.imgThreshold = 128;
                it.enableRotation = false; it.rotationStart = -45; it.rotationEnd = 45; it.rotationStep = 1;
                it.maxResults = 5; it.matchThreshold = 0.7f; it.maxImageDim = 1000; it.nmsThreshold = 0.3f; it.searchMode = 0;
TemplateState::ClearResults();
            }

            if (panelUi.PrimaryButton("执行模板匹配"))
            {
                if (ImageState::Current().empty())
                    LogSystem::Add(LOG_WARN, "请先加载图片");
                else if (it.templateImg.empty())
                    LogSystem::Add(LOG_WARN, "模板匹配: 请先抓取模板");
                else
                {
                    panelUi.RunToolFromCard(inst);
                }
            }

            panelUi.DrawSearchROIControls(it, inst);
            // ---- 模板 ----
            panelUi.SectionHeader("模板");

            const int templateCaptureROI = ToolAssetService::ActiveROIIndex(
                it.toolId, ToolAssetKind::TemplateMatch);

            if (templateCaptureROI < 0 && it.templateImg.empty())
            {
                if (panelUi.SecondaryButton("添加ROI获取模板"))
                    ToolAssetService::BeginROICapture(it, ToolAssetKind::TemplateMatch);
            }
            else if (templateCaptureROI >= 0)
            {
                ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "模板ROI已就绪");
                ImGui::TextDisabled("拖拽ROI调整位置后点击确认");
                if (panelUi.PrimaryButton("确认捕获"))
                {
                    const ToolAssetCaptureResult result = ToolAssetService::ConfirmROICapture(
                        it, ToolAssetKind::TemplateMatch);
                    if (result.success)
                    {
                        LogSystem::Add(LOG_INFO, "模板匹配: 模板已抓取 %dx%d",
                            result.bounds.width, result.bounds.height);
                        panelUi.MarkCurrentRecipeAssetsDirty();
                    }
                    else
                        LogSystem::Add(LOG_WARN, "模板匹配: ROI区域无效或超出图像范围");
                }
                if (panelUi.SecondaryButton("取消"))
                    ToolAssetService::CancelROICapture(it.toolId, ToolAssetKind::TemplateMatch);
            }
            else if (!it.templateImg.empty())
            {
                if (panelUi.SecondaryButton("修改模板ROI"))
                    ToolAssetService::BeginROICapture(it, ToolAssetKind::TemplateMatch);
                if (panelUi.SecondaryButton("清除模板"))
                {
                    ToolAssetService::ClearAsset(it, ToolAssetKind::TemplateMatch);
                    panelUi.MarkCurrentRecipeAssetsDirty();
                }
            }

            if (!it.templateImg.empty())
            {
                // 显示预览开关
                ImGui::Checkbox("显示预览##tm", &it.showTemplatePreview);
                if (it.showTemplatePreview)
                {
                    std::uint64_t signature = PreviewTextureCache::ImageSignature(it.templateImg);
                    signature = PreviewTextureCache::CombineSignature(signature, it.tplGray);
                    signature = PreviewTextureCache::CombineSignature(signature, it.tplBinary);
                    signature = PreviewTextureCache::CombineSignature(signature, it.tplBinThresh);
                    signature = PreviewTextureCache::CombineSignature(signature, it.tplEdge);
                    signature = PreviewTextureCache::CombineSignature(signature, it.tplEdgeLow);
                    signature = PreviewTextureCache::CombineSignature(signature, it.tplEdgeHigh);
                    if (PreviewTextureCache::NeedsUpdate(it.toolId, PreviewTextureKind::TemplateMatch, signature))
                    {
                        cv::Mat preview = it.templateImg.clone();
                        if (it.tplGray && preview.channels() > 1)
                            cv::cvtColor(preview, preview, cv::COLOR_BGR2GRAY);
                        if (it.tplBinary)
                        {
                            if (preview.channels() > 1)
                                cv::cvtColor(preview, preview, cv::COLOR_BGR2GRAY);
                            cv::threshold(preview, preview, it.tplBinThresh, 255, cv::THRESH_BINARY);
                        }
                        if (it.tplEdge)
                        {
                            if (preview.channels() > 1)
                                cv::cvtColor(preview, preview, cv::COLOR_BGR2GRAY);
                            cv::Canny(preview, preview, it.tplEdgeLow, it.tplEdgeHigh);
                        }
                        PreviewTextureCache::Queue(it.toolId, PreviewTextureKind::TemplateMatch,
                            signature, preview, 80);
                    }

                    const PreviewTextureView preview = PreviewTextureCache::Get(
                        it.toolId, PreviewTextureKind::TemplateMatch);
                    const ImVec2 previewSize(preview.width * 2.0f, preview.height * 2.0f);
                    if (preview.ready)
                        ImGui::Image(preview.textureId, previewSize);
                    else
                        ImGui::Dummy(previewSize);
                    ImGui::SetItemTooltip("模板预览");
                    ImGui::TextDisabled("模板: %dx%d", it.templateImg.cols, it.templateImg.rows);
                }
            }
            else
                ImGui::TextDisabled("未抓取模板");

            // ---- 模板预处理 ----
            panelUi.SectionHeader("模板预处理");
            if (ImGui::BeginTable("##template_preprocess_flags", 3,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
            {
                ImGui::TableNextColumn();
                ImGui::Checkbox("灰度##tm", &it.tplGray);
                ImGui::TableNextColumn();
                ImGui::Checkbox("二值化##tm", &it.tplBinary);
                ImGui::TableNextColumn();
                ImGui::Checkbox("边缘##tm", &it.tplEdge);
                ImGui::EndTable();
            }
            if (it.tplBinary)
                ImGui::SliderInt("阈值##tm", &it.tplBinThresh, 0, 255);
            if (it.tplEdge)
            {
                ImGui::SliderInt("低##tm", &it.tplEdgeLow, 0, 255);
                ImGui::SliderInt("高##tm", &it.tplEdgeHigh, 0, 255);
            }

            // ---- 图像预处理 ----
            panelUi.SectionHeader("图像预处理");
            if (ImGui::BeginTable("##image_preprocess_flags", 2,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
            {
                ImGui::TableNextColumn();
                ImGui::Checkbox("转为灰度##tm_i", &it.imgUseGray);
                ImGui::TableNextColumn();
                ImGui::Checkbox("二值化##tm_i", &it.imgEnableThreshold);
                ImGui::EndTable();
            }
            if (it.imgEnableThreshold)
                ImGui::SliderInt("阈值##tm_i", &it.imgThreshold, 0, 255);

            panelUi.SectionHeader("旋转");
            ImGui::Checkbox("启用旋转", &it.enableRotation);
            if (it.enableRotation)
            {
                if (ImGui::BeginTable("##rotation_range", 3,
                    ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
                {
                    const char* labels[] = {"起始°", "结束°", "步长°"};
                    int* values[] = {&it.rotationStart, &it.rotationEnd, &it.rotationStep};
                    const int minimums[] = {-45, 0, 1};
                    const int maximums[] = {0, 45, 10};
                    for (int column = 0; column < 3; ++column)
                    {
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("%s", labels[column]);
                        ImGui::SetNextItemWidth(-1.0f);
                        ImGui::PushID(column);
                        ImGui::SliderInt("##rotation_value", values[column],
                            minimums[column], maximums[column]);
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }

            // ---- 匹配参数 ----
            panelUi.SectionHeader("匹配参数");
            ImGui::SliderInt("最大结果数", &it.maxResults, 1, 100);
            ImGui::SliderFloat("匹配阈值", &it.matchThreshold, 0.0f, 1.0f, "%.3f");
            ImGui::SliderFloat("NMS阈值", &it.nmsThreshold, 0.0f, 1.0f, "%.3f");
            ImGui::Checkbox("亚像素位置/角度##tm", &it.matchSubpixel);
            ImGui::SetItemTooltip("对相关峰进行二次曲线插值，并忽略旋转模板的域外边角");
            ImGui::SliderInt("匹配精度", &it.maxImageDim, 400, 2000);

            // ---- 操作 ----
            if (panelUi.SecondaryButton("清空结果"))
            {
                it.lastResult = ToolResult{};
                it.hasLastResult = false;
                TemplateState::ClearResults();
            }

            panelUi.EndCard();
        };

        // 4: YOLO
        panels[4] = [panelUi](ToolInstance &it, int inst)
        {
            panelUi.BeginCard("YOLO检测");
            if (panelUi.SecondaryButton("重置参数"))
            {
                it.yoloConfThreshold = 0.5f; it.yoloNmsThreshold = 0.4f;
                it.yoloUseROI = false; it.yoloUseGPU = false;
            }
            bool isLiveMode = VideoCapture::IsOpen();
            const bool yoloLiveDetect = ToolChainState::YoloLiveDetect();
            const int yoloLiveInstance = ToolChainState::YoloLiveInstanceIndex();
            const char *btnLabel = isLiveMode ? (yoloLiveDetect && yoloLiveInstance == inst ? "停止实时检测" : "开始实时检测") : "执行检测";
            bool isThisActive = (yoloLiveDetect && yoloLiveInstance == inst);
            if (panelUi.PrimaryButton(btnLabel))
            {
                if (!it.yoloModelPath.empty())
                    YOLODetector::LoadModel(it.yoloModelPath, it.yoloClassesPath, it.yoloUseGPU);
                if (isLiveMode)
                {
                    if (YOLODetector::IsLoaded())
                    {
                        if (isThisActive)
                        {
                            ToolChainState::SetYoloLiveDetect(false);
                            ToolChainState::SetYoloLiveInstanceIndex(-1);
                        }
                        else
                        {
                            ToolChainState::SetYoloLiveDetect(true);
                            ToolChainState::SetYoloLiveInstanceIndex(inst);
                            if (!VideoCapture::IsPlaying())
                                VideoCapture::Play();
                        }
                    }
                    else
                        LogSystem::Add(LOG_WARN, "YOLO: 请先选择模型文件");
                }
                else
                {
                    panelUi.RunToolFromCard(inst);
                }
            }
            panelUi.DrawSearchROIControls(it, inst);
            panelUi.SectionHeader("模型");
            if (ImGui::BeginTable("##yolo_model_files", 2,
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
            {
                const float pickerLabelWidth = (std::max)(
                    ImGui::CalcTextSize("选择 ONNX 模型").x,
                    ImGui::CalcTextSize("选择类别文件").x);
                const float pickerWidth = pickerLabelWidth +
                    ImGui::GetStyle().FramePadding.x * 2.0f;
                ImGui::TableSetupColumn("##picker", ImGuiTableColumnFlags_WidthFixed, pickerWidth);
                ImGui::TableSetupColumn("##file", ImGuiTableColumnFlags_WidthStretch);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (panelUi.SecondaryButton("选择 ONNX 模型"))
                {
                    std::string path = OpenFileDialogWithFilter(L"ONNX模型 (*.onnx)\0*.onnx\0所有文件 (*.*)\0*.*\0", L"选择 YOLO ONNX 模型文件");
                    if (!path.empty())
                        it.yoloModelPath = path;
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("%s", it.yoloModelPath.empty() ? "未选择" : FileName(it.yoloModelPath).c_str());
                if (!it.yoloModelPath.empty() && ImGui::IsItemHovered())
                    ImGui::SetItemTooltip("%s", it.yoloModelPath.c_str());

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (panelUi.SecondaryButton("选择类别文件"))
                {
                    std::string path = OpenFileDialogWithFilter(L"文本文件 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0", L"选择类别名称文件");
                    if (!path.empty())
                        it.yoloClassesPath = path;
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("%s", it.yoloClassesPath.empty() ? "默认 COCO 80 类" : FileName(it.yoloClassesPath).c_str());
                if (!it.yoloClassesPath.empty() && ImGui::IsItemHovered())
                    ImGui::SetItemTooltip("%s", it.yoloClassesPath.c_str());
                ImGui::EndTable();
            }
            panelUi.SectionHeader("参数");
            ImGui::SliderFloat("置信度阈值", &it.yoloConfThreshold, 0.1f, 1.0f, "%.2f");
            ImGui::SliderFloat("NMS阈值", &it.yoloNmsThreshold, 0.1f, 1.0f, "%.2f");
            float overlayOffsetX = RealtimeDetectionState::OverlayOffsetX();
            if (ImGui::SliderFloat("滚动补偿(X)", &overlayOffsetX, -100.0f, 100.0f, "%.0fpx"))
                RealtimeDetectionState::SetOverlayOffsetX(overlayOffsetX);
            if (ImGui::Checkbox("GPU加速(CUDA/DML)", &it.yoloUseGPU))
            {
                // LiveYoloRunner uses the currently active ORT session. Reload
                // immediately so changing this option during video playback
                // really switches backend instead of waiting for a later run.
                if (!it.yoloModelPath.empty())
                {
                    if (YOLODetector::LoadModel(
                        it.yoloModelPath, it.yoloClassesPath, it.yoloUseGPU))
                    {
                        LogSystem::Add(LOG_INFO,
                            "YOLO: 加速设置已应用，实际后端=%s",
                            YOLODetector::GetBackendName());
                    }
                }
            }
            ImGui::SetItemTooltip(
                "勾选后优先使用 CUDA，其次使用 DirectML；不可用时自动回退 CPU。\n"
                "实时检测中修改会立即重载当前模型。");
            panelUi.SectionHeader("状态");
            const char* actualYoloBackend = YOLODetector::GetBackendName();
            const bool yoloStillOnCpu = std::strcmp(actualYoloBackend, "CPU") == 0;
            if (yoloStillOnCpu)
            {
                ImGui::TextColored(
                    it.yoloUseGPU
                        ? ImVec4(1.0f, 0.55f, 0.25f, 1.0f)
                        : ImVec4(0.75f, 0.75f, 0.75f, 1.0f),
                    "实际后端: CPU%s",
                    it.yoloUseGPU ? "（GPU不可用，已回退）" : "（未启用GPU）");
            }
            else
            {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                    "实际后端: %s", actualYoloBackend);
            }
            if (ToolChainState::YoloLiveDetect() && ToolChainState::YoloLiveFrameMs() > 0)
                ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "实时: %.3fms", ToolChainState::YoloLiveFrameMs());
            panelUi.EndCard();
        };

        // 11: YOLO OpenCV 5.0 实验入口
        panels[11] = [panelUi](ToolInstance &it, int inst)
        {
            panelUi.BeginCard("YOLO OpenCV 5.0", "[实验] ");
            if (panelUi.SecondaryButton("重置参数"))
            {
                it.yoloConfThreshold = 0.5f;
                it.yoloNmsThreshold = 0.4f;
                it.yoloUseROI = false;
                it.yoloUseGPU = false;
            }

            if (panelUi.PrimaryButton("执行内置 OpenCV DNN 测试"))
                panelUi.RunToolFromCard(inst);

            const bool isOpenCV5Live = ToolChainState::YoloLiveDetect() &&
                ToolChainState::YoloLiveInstanceIndex() == inst;
            const char* liveLabel = isOpenCV5Live ? "停止实时测试##ocv5live" : "开始实时测试##ocv5live";
            if (panelUi.SecondaryButton(liveLabel))
            {
                if (isOpenCV5Live)
                {
                    ToolChainState::SetYoloLiveDetect(false);
                    ToolChainState::SetYoloLiveInstanceIndex(-1);
                    ToolChainState::SetYoloLiveFrameMs(0.0f);
                }
                else
                {
                    if (it.yoloModelPath.empty())
                    {
                        LogSystem::Add(LOG_WARN, "YOLO OpenCV DNN: 请先选择 ONNX 模型");
                    }
                    else
                    {
                        if (!VideoCapture::IsOpen())
                        {
                            LogSystem::Add(LOG_WARN, "YOLO OpenCV DNN: 请先加载视频或打开摄像头");
                        }
                        else
                        {
                            ToolChainState::SetYoloLiveDetect(true);
                            ToolChainState::SetYoloLiveInstanceIndex(inst);
                            if (!VideoCapture::IsPlaying())
                                VideoCapture::Play();
                        }
                    }
                }
            }
            if (isOpenCV5Live && ToolChainState::YoloLiveFrameMs() > 0)
                ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "实时: %.3fms/帧", ToolChainState::YoloLiveFrameMs());

            panelUi.DrawSearchROIControls(it, inst);
            panelUi.SectionHeader("模型");
            if (ImGui::BeginTable("##ocv5_model_files", 2,
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
            {
                const float pickerLabelWidth = (std::max)(
                    ImGui::CalcTextSize("选择 ONNX 模型").x,
                    ImGui::CalcTextSize("选择类别文件").x);
                const float pickerWidth = pickerLabelWidth +
                    ImGui::GetStyle().FramePadding.x * 2.0f;
                ImGui::TableSetupColumn("##picker", ImGuiTableColumnFlags_WidthFixed, pickerWidth);
                ImGui::TableSetupColumn("##file", ImGuiTableColumnFlags_WidthStretch);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (panelUi.SecondaryButton("选择 ONNX 模型##ocv5"))
                {
                    std::string path = OpenFileDialogWithFilter(L"ONNX模型 (*.onnx)\0*.onnx\0所有文件 (*.*)\0*.*\0", L"选择 YOLO ONNX 模型文件");
                    if (!path.empty())
                        it.yoloModelPath = path;
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("%s", it.yoloModelPath.empty() ? "未选择" : FileName(it.yoloModelPath).c_str());
                if (!it.yoloModelPath.empty() && ImGui::IsItemHovered())
                    ImGui::SetItemTooltip("%s", it.yoloModelPath.c_str());

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (panelUi.SecondaryButton("选择类别文件##ocv5"))
                {
                    std::string path = OpenFileDialogWithFilter(L"文本文件 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0", L"选择类别名称文件");
                    if (!path.empty())
                        it.yoloClassesPath = path;
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("%s", it.yoloClassesPath.empty() ? "默认 COCO 80 类" : FileName(it.yoloClassesPath).c_str());
                if (!it.yoloClassesPath.empty() && ImGui::IsItemHovered())
                    ImGui::SetItemTooltip("%s", it.yoloClassesPath.c_str());
                ImGui::EndTable();
            }

            panelUi.SectionHeader("参数");
            ImGui::SliderFloat("置信度阈值##ocv5", &it.yoloConfThreshold, 0.1f, 1.0f, "%.2f");
            ImGui::SliderFloat("NMS阈值##ocv5", &it.yoloNmsThreshold, 0.1f, 1.0f, "%.2f");

            panelUi.SectionHeader("状态");
            ImGui::TextDisabled("%s", OpenCVYoloDetector::IsLoaded() ? "OpenCV DNN 已加载" : "OpenCV DNN 未加载");

            panelUi.EndCard();
        };

        // 6: 形状匹配
        panels[6] = [panelUi](ToolInstance &it, int inst)
        {
            panelUi.BeginCard("形状匹配");
            if (panelUi.SecondaryButton("重置参数"))
            {
                ToolAssetService::ClearAsset(it, ToolAssetKind::ShapeTemplate); it.shpBlurSize = 5; it.shpTplRetr = 0;
                panelUi.MarkCurrentRecipeAssetsDirty();
                it.shpTplMinArea = 30; it.shpMinScore = 0.5f; it.shpShapeScore = 0.1f;
                it.shpLineThick = 2; it.shpMethod = 0; it.showResultLabels = true; it.shpMaxResults = 1;
                it.shpEnableRotation = false; it.shpRotationStart = -45;
                it.shpRotationEnd = 45; it.shpRotationStep = 5;
                it.shpTplGray = false; it.shpTplBinary = false; it.shpTplBinThresh = 128;
                it.shpTplBlur = false; it.shpTplBlurK = 5; it.shpTplInvert = false;
                it.showTemplatePreview = true;
            }
            if (ShapeMatcher::g_MatchTimeMs > 0)
                ImGui::TextDisabled("上次: %d个 %.3fms", ShapeMatcher::g_MatchCount, ShapeMatcher::g_MatchTimeMs);
            if (panelUi.PrimaryButton("执行形状匹配"))
            {
                if (ImageState::Current().empty())
                    LogSystem::Add(LOG_WARN, "请先加载图片");
                else if (it.shpTplImage.empty())
                    LogSystem::Add(LOG_WARN, "形状匹配: 请先冻结模板");
                else
                    panelUi.RunToolFromCard(inst);
            }

            panelUi.DrawSearchROIControls(it, inst);
            const int shapeCaptureROI = ToolAssetService::ActiveROIIndex(
                it.toolId, ToolAssetKind::ShapeTemplate);

            panelUi.SectionHeader("模板");

            // 未设置模板 / 想修改：显示"添加ROI"按钮
            if (shapeCaptureROI < 0 && it.shpTplImage.empty())
            {
                if (panelUi.SecondaryButton("添加ROI获取模板"))
                    ToolAssetService::BeginROICapture(it, ToolAssetKind::ShapeTemplate);
            }
            else if (shapeCaptureROI >= 0)
            {
                // ROI 已存在，显示操作按钮
                ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "模板ROI已就绪");
                ImGui::TextDisabled("拖拽ROI调整位置后点击确认");

                if (panelUi.PrimaryButton("确认捕获"))
                {
                    const ToolAssetCaptureResult result = ToolAssetService::ConfirmROICapture(
                        it, ToolAssetKind::ShapeTemplate);
                    if (result.success)
                    {
                        LogSystem::Add(LOG_INFO, "形状匹配: 模板已捕获 %dx%d",
                            result.bounds.width, result.bounds.height);
                        panelUi.MarkCurrentRecipeAssetsDirty();
                    }
                    else
                        LogSystem::Add(LOG_WARN, "形状匹配: ROI 区域无效");
                }
                if (panelUi.SecondaryButton("取消"))
                    ToolAssetService::CancelROICapture(it.toolId, ToolAssetKind::ShapeTemplate);
            }

            // ---- 第一块：按钮（修改/清除） ----
            if (!it.shpTplImage.empty())
            {
                if (shapeCaptureROI < 0)
                {
                    if (panelUi.SecondaryButton("修改模板ROI"))
                        ToolAssetService::BeginROICapture(it, ToolAssetKind::ShapeTemplate);
                }
                if (panelUi.SecondaryButton("清除模板"))
                {
                    ToolAssetService::ClearAsset(it, ToolAssetKind::ShapeTemplate);
                    panelUi.MarkCurrentRecipeAssetsDirty();
                }
            }

            // ---- 第二块：预览渲染（独立重新检查） ----
            if (!it.shpTplImage.empty())
            {
                ImGui::Checkbox("显示预览##shp", &it.showTemplatePreview);
                if (it.showTemplatePreview)
                {
                    std::uint64_t signature = PreviewTextureCache::ImageSignature(it.shpTplImage);
                    signature = PreviewTextureCache::CombineSignature(signature, it.shpTplGray);
                    signature = PreviewTextureCache::CombineSignature(signature, it.shpTplBlur);
                    signature = PreviewTextureCache::CombineSignature(signature, it.shpTplBlurK);
                    signature = PreviewTextureCache::CombineSignature(signature, it.shpTplBinary);
                    signature = PreviewTextureCache::CombineSignature(signature, it.shpTplBinThresh);
                    signature = PreviewTextureCache::CombineSignature(signature, it.shpTplInvert);
                    if (PreviewTextureCache::NeedsUpdate(it.toolId, PreviewTextureKind::ShapeTemplate, signature))
                    {
                        cv::Mat preview = it.shpTplImage.clone();
                        if (it.shpTplGray && preview.channels() > 1)
                            cv::cvtColor(preview, preview, cv::COLOR_BGR2GRAY);
                        if (it.shpTplBlur)
                            cv::GaussianBlur(preview, preview,
                                cv::Size(it.shpTplBlurK | 1, it.shpTplBlurK | 1), 0);
                        if (it.shpTplBinary)
                        {
                            if (preview.channels() > 1)
                                cv::cvtColor(preview, preview, cv::COLOR_BGR2GRAY);
                            cv::threshold(preview, preview, it.shpTplBinThresh, 255, cv::THRESH_BINARY);
                        }
                        if (it.shpTplInvert)
                            cv::bitwise_not(preview, preview);
                        PreviewTextureCache::Queue(it.toolId, PreviewTextureKind::ShapeTemplate,
                            signature, preview, 80);
                    }

                    const PreviewTextureView preview = PreviewTextureCache::Get(
                        it.toolId, PreviewTextureKind::ShapeTemplate);
                    const ImVec2 previewSize(preview.width * 2.0f, preview.height * 2.0f);
                    if (preview.ready)
                        ImGui::Image(preview.textureId, previewSize);
                    else
                        ImGui::Dummy(previewSize);
                    ImGui::TextDisabled("模板: %dx%d", it.shpTplImage.cols, it.shpTplImage.rows);
                }
            }
            else if (shapeCaptureROI < 0)
                ImGui::TextDisabled("未设置模板");

            panelUi.SectionHeader("模板预处理");
            if (ImGui::BeginTable("##shape_preprocess_options", 2,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
            {
                ImGui::TableNextColumn();
                ImGui::Checkbox("灰度##shp", &it.shpTplGray);
                ImGui::TableNextColumn();
                ImGui::Checkbox("模糊##shp", &it.shpTplBlur);
                ImGui::TableNextColumn();
                ImGui::Checkbox("二值化##shp", &it.shpTplBinary);
                ImGui::TableNextColumn();
                ImGui::Checkbox("反色##shp", &it.shpTplInvert);
                ImGui::EndTable();
            }
            if (it.shpTplBlur)
                ImGui::SliderInt("模糊核##shp", &it.shpTplBlurK, 1, 15);
            if (it.shpTplBinary)
                ImGui::SliderInt("阈值##shp", &it.shpTplBinThresh, 0, 255);

            panelUi.SectionHeader("搜索参数");
            ImGui::SliderInt("模糊##shp_s", &it.shpBlurSize, 0, 20);
            ImGui::InputFloat("最小面积##shp", &it.shpTplMinArea, 10, 100, "%.0f");
            const char *retrNames[] = {"EXTERNAL", "LIST", "TREE"};
            ImGui::Combo("轮廓检索##shp", &it.shpTplRetr, retrNames, 3);
            ImGui::SliderFloat("匹配阈值", &it.shpMinScore, 0.1f, 1.0f, "%.3f");
            ImGui::SliderFloat("形状阈值", &it.shpShapeScore, 0.05f, 1.0f, "%.3f");
            const char *methodNames[] = {"Hu矩", "ShapeContext", "Hausdorff"};
            ImGui::Combo("方法##shp", &it.shpMethod, methodNames, 3);
            ImGui::SliderInt("线宽##shp", &it.shpLineThick, 1, 5);
            ImGui::SliderInt("最多##shp", &it.shpMaxResults, 1, 200);
            ImGui::Checkbox("启用形状模型角度搜索##shp", &it.shpEnableRotation);
            if (it.shpEnableRotation)
            {
                ImGui::SliderInt("起始角度##shp", &it.shpRotationStart, -180, 180);
                ImGui::SliderInt("结束角度##shp", &it.shpRotationEnd, -180, 180);
                ImGui::SliderInt("角度步长##shp", &it.shpRotationStep, 1, 30);
            }
            panelUi.EndCard();
        };

        // 10: 多点找色
        panels[10] = [panelUi](ToolInstance &it, int inst)
        {
            panelUi.BeginCard("多点找色");

            // 兼容旧配方：只补齐本工具 ROI，不再写入全局 ROI。
            static std::unordered_map<std::uint64_t, bool> s_mcfRoiRestored;
            if (it.searchROIs.empty() && it.mcfRoiW > 0 && !s_mcfRoiRestored[it.toolId])
            {
                ROI r; r.type = ROI_TYPE_RECT;
                r.start = ImVec2((float)it.mcfRoiX, (float)it.mcfRoiY);
                r.end   = ImVec2((float)(it.mcfRoiX + it.mcfRoiW), (float)(it.mcfRoiY + it.mcfRoiH));
                it.searchROIs.push_back(r);
                it.lineSaveROIs = it.searchROIs;
                s_mcfRoiRestored[it.toolId] = true;
            }

            if (panelUi.SecondaryButton("重置参数"))
            {
                ToolAssetService::ClearAsset(it, ToolAssetKind::MultiColorReference);
                panelUi.MarkCurrentRecipeAssetsDirty();
                it.mcfShowPreview = true;
                it.mcfImgGray = false; it.mcfImgBinary = false; it.mcfImgBinThresh = 128;
                it.mcfUseROI = false; it.mcfMaxResults = 1;
                it.mcfMinDist = 5.0f; it.mcfCrossSize = 10; it.mcfCrossThick = 2;
                if (it.toolImpl)
                {
                    auto* mf = dynamic_cast<MultiColorFinder*>(it.toolImpl.get());
                    if (mf) { mf->points.clear(); mf->refImage.release(); }
                }
            }

            bool hasPoints = false;
            if (it.toolImpl)
            {
                auto* mf = dynamic_cast<MultiColorFinder*>(it.toolImpl.get());
                if (mf) hasPoints = !mf->points.empty();
            }
            if (ToolChainState::McfLastCount() > 0)
                ImGui::TextDisabled("上次匹配: %d 个", ToolChainState::McfLastCount());
            if (panelUi.PrimaryButton("执行多点找色"))
            {
                if (it.mcfRefImage.empty())
                    LogSystem::Add(LOG_WARN, "请先抓取参考图");
                else if (!hasPoints)
                    LogSystem::Add(LOG_WARN, "请在参考图上点击取色（至少1个点）");
                else
                    panelUi.RunToolFromCard(inst);
            }

            panelUi.DrawSearchROIControls(it, inst);
            // ---- 参考图（对齐模板匹配的ROI捕获流程） ----
            panelUi.SectionHeader("参考图");

            const int mcfCaptureROI = ToolAssetService::ActiveROIIndex(
                it.toolId, ToolAssetKind::MultiColorReference);

            // === 阶段1: 没有ROI也没有参考图 ===
            if (mcfCaptureROI < 0 && it.mcfRefImage.empty())
            {
                if (panelUi.SecondaryButton("添加ROI获取参考图"))
                    ToolAssetService::BeginROICapture(it, ToolAssetKind::MultiColorReference);
            }
            // === 阶段2: ROI已激活，等待确认 ===
            else if (mcfCaptureROI >= 0)
            {
                ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "参考图ROI已就绪");
                ImGui::TextDisabled("拖拽ROI调整位置后点击确认");
                if (panelUi.PrimaryButton("确认捕获"))
                {
                    const ToolAssetCaptureResult result = ToolAssetService::ConfirmROICapture(
                        it, ToolAssetKind::MultiColorReference);
                    if (result.success)
                    {
                        LogSystem::Add(LOG_INFO, "多点找色: 参考图已抓取 %dx%d",
                            result.bounds.width, result.bounds.height);
                        panelUi.MarkCurrentRecipeAssetsDirty();
                    }
                    else
                        LogSystem::Add(LOG_WARN, "多点找色: ROI区域无效或超出图像范围");
                }
                if (panelUi.SecondaryButton("取消"))
                    ToolAssetService::CancelROICapture(it.toolId, ToolAssetKind::MultiColorReference);
            }
            // === 阶段3: 参考图已就绪 ===
            else if (!it.mcfRefImage.empty())
            {
                if (panelUi.SecondaryButton("修改参考图ROI"))
                    ToolAssetService::BeginROICapture(it, ToolAssetKind::MultiColorReference);
                if (panelUi.SecondaryButton("清除参考图"))
                {
                    ToolAssetService::ClearAsset(it, ToolAssetKind::MultiColorReference);
                    if (it.toolImpl)
                    {
                        auto* mf = dynamic_cast<MultiColorFinder*>(it.toolImpl.get());
                        if (mf) { mf->points.clear(); mf->refImage.release(); }
                    }
                    panelUi.MarkCurrentRecipeAssetsDirty();
                }
            }

            // ---- 参考图预览 + 点击取色（仅当参考图存在） ----
            if (!it.mcfRefImage.empty())
            {
                if (ImGui::BeginTable("##mcf_preview_header", 2,
                    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
                {
                    const char* previewAction = it.mcfShowPreview ? "隐藏预览" : "显示预览";
                    const float previewActionWidth = ImGui::CalcTextSize(previewAction).x +
                        ImGui::GetStyle().FramePadding.x * 2.0f;
                    ImGui::TableSetupColumn("##description", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("##action", ImGuiTableColumnFlags_WidthFixed,
                        previewActionWidth);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1),
                        "参考图 %dx%d", it.mcfRefImage.cols, it.mcfRefImage.rows);
                    ImGui::TableSetColumnIndex(1);
                    if (panelUi.SecondaryButton(previewAction, -1.0f))
                        it.mcfShowPreview = !it.mcfShowPreview;
                    ImGui::EndTable();
                }

                if (it.mcfShowPreview)
                {
                auto ReadBgrAt = [](const cv::Mat& mat, int y, int x, uchar& b, uchar& g, uchar& r) -> bool
                {
                    if (mat.empty() || mat.depth() != CV_8U ||
                        (unsigned)x >= (unsigned)mat.cols || (unsigned)y >= (unsigned)mat.rows)
                        return false;
                    const int ch = mat.channels();
                    const uchar* p = mat.ptr<uchar>(y) + (size_t)x * ch;
                    if (ch == 1)
                    {
                        b = g = r = p[0];
                        return true;
                    }
                    if (ch >= 3)
                    {
                        b = p[0];
                        g = p[1];
                        r = p[2];
                        return true;
                    }
                    return false;
                };

                std::uint64_t signature = PreviewTextureCache::ImageSignature(it.mcfRefImage);
                signature = PreviewTextureCache::CombineSignature(signature, it.mcfImgGray);
                signature = PreviewTextureCache::CombineSignature(signature, it.mcfImgBinary);
                signature = PreviewTextureCache::CombineSignature(signature, it.mcfImgBinThresh);
                if (PreviewTextureCache::NeedsUpdate(
                    it.toolId, PreviewTextureKind::MultiColorReference, signature))
                {
                    cv::Mat preview = it.mcfRefImage.clone();
                    if (it.mcfImgGray && preview.channels() > 1)
                        cv::cvtColor(preview, preview, cv::COLOR_BGR2GRAY);
                    if (it.mcfImgBinary)
                    {
                        if (preview.channels() > 1)
                            cv::cvtColor(preview, preview, cv::COLOR_BGR2GRAY);
                        cv::threshold(preview, preview, it.mcfImgBinThresh, 255, cv::THRESH_BINARY);
                    }
                    PreviewTextureCache::Queue(it.toolId, PreviewTextureKind::MultiColorReference,
                        signature, preview, 120);
                }

                const PreviewTextureView preview = PreviewTextureCache::Get(
                    it.toolId, PreviewTextureKind::MultiColorReference);
                const ImVec2 base = ImGui::GetCursorScreenPos();
                const ImVec2 previewSize(preview.width * 2.0f, preview.height * 2.0f);
                if (preview.ready)
                    ImGui::Image(preview.textureId, previewSize);
                else
                    ImGui::Dummy(previewSize);

                ImDrawList* dl = ImGui::GetWindowDrawList();
                const float scaleX = preview.width > 0
                    ? previewSize.x / static_cast<float>(it.mcfRefImage.cols) : 0.0f;
                const float scaleY = preview.height > 0
                    ? previewSize.y / static_cast<float>(it.mcfRefImage.rows) : 0.0f;

                // ---- 在小图上绘制已选颜色点的红色标记 ----
                if (it.toolImpl)
                {
                    auto* mf = dynamic_cast<MultiColorFinder*>(it.toolImpl.get());
                    if (mf && !mf->points.empty())
                    {
                        float markerR = 3.0f;
                        int ax = it.mcfAnchorX, ay = it.mcfAnchorY;
                        for (int pi = 0; pi < (int)mf->points.size(); pi++)
                        {
                            const auto& pt = mf->points[pi];
                            float sx = base.x + (ax + pt.x) * scaleX;
                            float sy = base.y + (ay + pt.y) * scaleY;
                            ImU32 mkCol = (pi == 0) ? IM_COL32(255, 60, 60, 255) : IM_COL32(255, 120, 60, 255);
                            // 红色十字
                            dl->AddLine(ImVec2(sx - markerR, sy), ImVec2(sx + markerR, sy), mkCol, 1.5f);
                            dl->AddLine(ImVec2(sx, sy - markerR), ImVec2(sx, sy + markerR), mkCol, 1.5f);
                            // 编号
                            char num[8]; snprintf(num, sizeof(num), "%d", pi + 1);
                            dl->AddText(ImVec2(sx + 4, sy - 8), IM_COL32(255, 255, 100, 255), num);
                        }
                    }
                }

                // 点击取色
                if (ImGui::IsItemClicked())
                {
                    ImVec2 mouse = ImGui::GetMousePos();
                    int px = scaleX > 0.0f ? static_cast<int>((mouse.x - base.x) / scaleX) : -1;
                    int py = scaleY > 0.0f ? static_cast<int>((mouse.y - base.y) / scaleY) : -1;
                    if (px >= 0 && px < it.mcfRefImage.cols && py >= 0 && py < it.mcfRefImage.rows)
                    {
                        uchar b = 0, g = 0, r = 0;
                        if (!ReadBgrAt(it.mcfRefImage, py, px, b, g, r))
                        {
                            LogSystem::Add(LOG_WARN, "取色失败: 图像格式不支持或坐标越界");
                        }
                        else
                        {
                            if (it.mcfImgGray || it.mcfImgBinary)
                            {
                                uchar gray = cv::saturate_cast<uchar>(
                                    0.114f * b + 0.587f * g + 0.299f * r);
                                if (it.mcfImgBinary)
                                    gray = gray > it.mcfImgBinThresh ? 255 : 0;
                                b = g = r = gray;
                            }
                            ColorPoint cp;
                            cp.b = b; cp.g = g; cp.r = r;
                            cp.tolerance = 10;
                            if (!it.toolImpl) it.toolImpl = ITool::Create(10);
                            auto* mf = dynamic_cast<MultiColorFinder*>(it.toolImpl.get());
                            // 首点设为锚点，后续点偏移相对于锚点
                            if (mf && mf->points.empty())
                            {
                                it.mcfAnchorX = px;
                                it.mcfAnchorY = py;
                                cp.x = 0;
                                cp.y = 0;
                            }
                            else
                            {
                                cp.x = px - it.mcfAnchorX;
                                cp.y = py - it.mcfAnchorY;
                            }
                            if (mf) mf->points.push_back(cp);
                            LogSystem::Add(LOG_INFO, "取色: (%d,%d) BGR(%d,%d,%d)", cp.x, cp.y, cp.b, cp.g, cp.r);
                        }
                    }
                }
                ImGui::SetItemTooltip("点击取色 | 首点=锚点(0,0) | 后续点=相对偏移");

                // ---- 颜色点列表（色块展示） ----
                if (it.toolImpl)
                {
                    auto* mf = dynamic_cast<MultiColorFinder*>(it.toolImpl.get());
                    if (mf && !mf->points.empty())
                    {
                        ImGui::Separator();
                        ImGui::Text("已选颜色点 (%d个):", (int)mf->points.size());

                        // 全局容差滑块 — 直接控制所有点的容差 + 实时重新匹配
                        int allTol = mf->points[0].tolerance;
                        panelUi.ParamLabel("统一容差");
                        if (ImGui::SliderInt("##all_tolerance", &allTol, 0, 128))
                        {
                            for (auto& pt : mf->points)
                                pt.tolerance = allTol;
                            // 实时重新执行匹配
                            if (!ImageState::Current().empty() && !it.mcfRefImage.empty() && !mf->points.empty())
                                ToolController::RequestRun(inst);
                        }

                        int removeIdx = -1;
                        for (int pi = 0; pi < (int)mf->points.size(); pi++)
                        {
                            auto& pt = mf->points[pi];
                            ImGui::PushID(pi);
                            if (ImGui::BeginTable("##color_point_header", 3,
                                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
                            {
                                ImGui::TableSetupColumn("##swatch", ImGuiTableColumnFlags_WidthFixed,
                                    ImGui::GetFrameHeight());
                                ImGui::TableSetupColumn("##description", ImGuiTableColumnFlags_WidthStretch);
                                ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed,
                                    ImGui::GetFrameHeight());
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                const ImU32 swatch = IM_COL32(pt.r, pt.g, pt.b, 255);
                                const float swatchSize = ImGui::GetTextLineHeight();
                                const ImVec2 cp = ImGui::GetCursorScreenPos();
                                dl->AddRectFilled(cp, ImVec2(cp.x + swatchSize, cp.y + swatchSize), swatch);
                                dl->AddRect(cp, ImVec2(cp.x + swatchSize, cp.y + swatchSize),
                                    IM_COL32(255, 255, 255, 80));
                                ImGui::Dummy(ImVec2(swatchSize, swatchSize));

                                ImGui::TableSetColumnIndex(1);
                                const std::string pointLabel = pi == 0
                                    ? "锚点"
                                    : cv::format("偏移(%+d,%+d)", pt.x, pt.y);
                                ImGui::Text("%s  BGR(%d,%d,%d)", pointLabel.c_str(),
                                    pt.b, pt.g, pt.r);

                                ImGui::TableSetColumnIndex(2);
                                if (panelUi.SecondaryButton("X", ImGui::GetFrameHeight()))
                                    removeIdx = pi;
                                ImGui::EndTable();
                            }
                            panelUi.ParamLabel("容差");
                            if (ImGui::SliderInt("##tol", &pt.tolerance, 0, 128))
                            {
                                // 单个点容差变化也实时重新匹配
                                if (!ImageState::Current().empty() && !it.mcfRefImage.empty() && !mf->points.empty())
                                    ToolController::RequestRun(inst);
                            }
                            if (pi + 1 < static_cast<int>(mf->points.size()))
                                ImGui::Separator();
                            ImGui::PopID();
                        }
                        if (removeIdx >= 0)
                            mf->points.erase(mf->points.begin() + removeIdx);
                    }
                }
                } // mcfShowPreview
            }

            // ---- 图像预处理 ----
            panelUi.SectionHeader("图像预处理");
            auto UpdatePointColors = [&]() {
                if (!it.mcfRefImage.empty() && it.toolImpl)
                {
                    auto* mf = dynamic_cast<MultiColorFinder*>(it.toolImpl.get());
                    if (!mf || mf->points.empty()) return;
                    cv::Mat refProc = it.mcfRefImage.clone();
                    if (it.mcfImgGray && refProc.channels() > 1)
                        cv::cvtColor(refProc, refProc, cv::COLOR_BGR2GRAY);
                    if (it.mcfImgBinary)
                    {
                        if (refProc.channels() > 1) cv::cvtColor(refProc, refProc, cv::COLOR_BGR2GRAY);
                        cv::threshold(refProc, refProc, it.mcfImgBinThresh, 255, cv::THRESH_BINARY);
                    }
                    for (auto& cp : mf->points)
                    {
                        int px = cp.x + it.mcfAnchorX;
                        int py = cp.y + it.mcfAnchorY;
                        if ((unsigned)px < (unsigned)refProc.cols && (unsigned)py < (unsigned)refProc.rows)
                        {
                            const int ch = refProc.channels();
                            const uchar* p = refProc.ptr<uchar>(py) + (size_t)px * ch;
                            if (ch == 1)
                            {
                                cp.b = cp.g = cp.r = p[0];
                            }
                            else if (ch >= 3)
                            {
                                cp.b = p[0];
                                cp.g = p[1];
                                cp.r = p[2];
                            }
                        }
                    }
                }
            };
            if (ImGui::BeginTable("##mcf_preprocess_options", 2,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
            {
                ImGui::TableNextColumn();
                if (ImGui::Checkbox("转为灰度##mcf", &it.mcfImgGray))
                    { McfApplyPreview(it.mcfImgGray, it.mcfImgBinary, it.mcfImgBinThresh, ImageState::Current()); UpdatePointColors(); }
                ImGui::TableNextColumn();
                if (ImGui::Checkbox("二值化##mcf", &it.mcfImgBinary))
                    { McfApplyPreview(it.mcfImgGray, it.mcfImgBinary, it.mcfImgBinThresh, ImageState::Current()); UpdatePointColors(); }
                ImGui::EndTable();
            }
            if (it.mcfImgBinary && ImGui::SliderInt("阈值##mcf", &it.mcfImgBinThresh, 0, 255))
                { McfApplyPreview(it.mcfImgGray, it.mcfImgBinary, it.mcfImgBinThresh, ImageState::Current()); UpdatePointColors(); }
            ImGui::TextDisabled("预处理同时应用于主图和参考图");

            // ---- 搜索参数 ----
            panelUi.SectionHeader("搜索");
            ImGui::SliderInt("最大结果数", &it.mcfMaxResults, 1, 200);
            ImGui::SliderFloat("去重距离", &it.mcfMinDist, 0, 50, "%.0fpx");
            ImGui::SliderInt("十字大小", &it.mcfCrossSize, 3, 30);
            ImGui::SliderInt("十字粗细", &it.mcfCrossThick, 1, 5);

            panelUi.EndCard();
        };
}
