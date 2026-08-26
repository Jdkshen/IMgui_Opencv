#include "MeasurementToolPanel.h"

#include "../ROIManager.h"
#include "../../Core/CalibrationFitter.h"
#include "../../Core/ImageState.h"
#include "../../Core/OpenFileDialog.h"
#include "../../Core/ROIState.h"
#include "../../Core/ToolROIService.h"
#include "../../Log/LogSystem.h"
#include "../../include/imgui/imgui.h"

#include <algorithm>
#include <vector>

namespace
{
int s_measurementRoiDrawOwner = -1;
bool s_measurementRoiModifying = false;
std::vector<ROI> s_measurementRoiPendingBackup;

void BeginMeasurementRoiDrawSequence(int mode)
{
    switch (mode)
    {
    case 0: UI::BeginROIDrawSequence({ROI_TYPE_POINT, ROI_TYPE_POINT}); break;
    case 1: UI::BeginROIDrawSequence({ROI_TYPE_RECT}); break;
    case 2: UI::BeginROIDrawSequence({ROI_TYPE_LINE, ROI_TYPE_LINE}); break;
    case 3: UI::BeginROIDrawSequence({ROI_TYPE_CIRCLE}); break;
    case 4: UI::BeginROIDrawSequence({ROI_TYPE_RECT}); break;
    case 5: UI::BeginROIDrawSequence({ROI_TYPE_RECT}); break;
    case 6: UI::BeginROIDrawSequence({ROI_TYPE_POINT, ROI_TYPE_LINE}); break;
    case 7: UI::BeginROIDrawSequence({ROI_TYPE_LINE, ROI_TYPE_LINE}); break;
    default: UI::BeginROIDrawSequence({ROI_TYPE_POINT, ROI_TYPE_POINT}); break;
    }
}

const char* RoiTypeDisplayName(int type)
{
    switch (type)
    {
    case ROI_TYPE_RECT: return "矩形";
    case ROI_TYPE_POINT: return "点";
    case ROI_TYPE_LINE: return "线段";
    case ROI_TYPE_CIRCLE: return "圆";
    case ROI_TYPE_POLYGON: return "多边形";
    default: return "ROI";
    }
}

bool SyncMeasurementRuntimeRois(ToolInstance& tool)
{
    return ToolROIService::SyncMeasurementROIs(tool);
}

void RemoveMeasurementRuntimeRois(ToolInstance& tool)
{
    ToolROIService::RemoveMeasurementROIs(tool);
}
}

void RegisterMeasurementToolPanel(
    std::unordered_map<int, ToolUIFn>& panels,
    const ToolPanelContext& ui)
{
        // 15: 工业测量
        panels[15] = [ui](ToolInstance& it, int inst)
        {
            ui.beginCard("工业测量");
            SyncMeasurementRuntimeRois(it);
            auto StartMeasurementROIDrawing = [&](bool preserveExisting = false)
            {
                if (preserveExisting && !it.searchROIs.empty())
                {
                    // 兼容旧配方：旧 ROI 没有 runtimeId 或未恢复到 ROIState 时，
                    // 先按几何位置重新挂回当前 ROI 列表。
                    ToolROIService::RestoreMeasurementROIs(it);
                }
                s_measurementRoiModifying = preserveExisting;
                s_measurementRoiPendingBackup = preserveExisting ? it.searchROIs : std::vector<ROI>{};
                s_measurementRoiDrawOwner = inst;
                if (preserveExisting && !it.searchROIs.empty())
                {
                    UI::CancelROIDrawSequence();
                    UI::gCurrentROIType = it.searchROIs.front().type;
                    ToolROIService::SelectMeasurementROI(it);
                    UI::gDrawingROI = false;
                }
                else
                {
                    RemoveMeasurementRuntimeRois(it);
                    BeginMeasurementRoiDrawSequence(it.measureMode);
                }
            };
            if (ui.secondaryButton("重置参数"))
            {
                it.measureMode = 0;
                it.measureCaliperCount = 16;
                it.measureSearchLength = 30.0f;
                it.measureProjectionWidth = 5.0f;
                it.measureSmoothingSigma = 1.0f;
                it.measureEdgeThreshold = 12.0f;
                it.measureMinPairDistance = 3.0f;
                it.measureEdgePolarity = 0;
                it.measureSubpixel = true;
                it.measureFitMethod = 1;
                it.measureFitInlierThreshold = 1.5f;
                it.measureMinimumValidCalipers = 3;
                it.measureMinimumConfidence = 0.0f;
                it.measureMmPerPixel = 0.0f;
                it.measureCalibrationPixels = 100.0f;
                it.measureCalibrationMm = 10.0f;
                it.measureCalibration = CalibrationModel{};
                it.measureToleranceEnabled = false;
                it.measureNominal = 0.0f;
                it.measureToleranceMinus = 0.0f;
                it.measureTolerancePlus = 0.0f;
                StartMeasurementROIDrawing();
            }
            if (ui.primaryButton("执行测量"))
                ui.runTool(inst);

            ui.sectionHeader("测量参数");
            const char* modes[] = {
                "点点距离", "边缘对/宽度卡尺", "线线角度", "圆拟合/直径",
                "边缘点卡尺", "直线拟合", "点线距离", "线线距离"
            };
            it.measureMode = std::clamp(it.measureMode, 0, 7);
            if (ImGui::Combo("测量类型##measure", &it.measureMode, modes, IM_ARRAYSIZE(modes)))
            {
                StartMeasurementROIDrawing();
                ui.saveRecipe();
            }

            ui.sectionHeader("测量 ROI");
            if (ui.secondaryButton("按当前测量类型绘制 ROI"))
                StartMeasurementROIDrawing();

            if (s_measurementRoiDrawOwner == inst)
            {
                std::vector<ROI> completedROIs;
                if (UI::ConsumeCompletedROIDrawSequence(completedROIs))
                {
                    it.searchROIs = std::move(completedROIs);
                    it.lineSaveROIs = it.searchROIs;
                    it.useSearchROI = true;
                    it.measureRuntimeROIIds.clear();
                    it.measureRuntimeROIIds.reserve(it.searchROIs.size());
                    for (const ROI& roi : it.searchROIs)
                        it.measureRuntimeROIIds.push_back(roi.runtimeId);
                    s_measurementRoiPendingBackup.clear();
                    s_measurementRoiModifying = false;
                    s_measurementRoiDrawOwner = -1;
                    ui.saveRecipe();
                }
            }

            if (s_measurementRoiDrawOwner == inst && s_measurementRoiModifying)
            {
                ImGui::TextColored(ImVec4(0.35f, 0.8f, 1.0f, 1.0f),
                    "请在图像中拖动 ROI 控制点或中心位置");
                if (ui.primaryButton("完成修改##measurement_roi_apply"))
                {
                    SyncMeasurementRuntimeRois(it);
                    s_measurementRoiPendingBackup.clear();
                    s_measurementRoiModifying = false;
                    s_measurementRoiDrawOwner = -1;
                    ROIState::SetSelectedIndex(-1);
                    ui.saveRecipe();
                }
                if (ui.secondaryButton("取消修改##measurement_roi_cancel"))
                {
                    ToolROIService::RestoreMeasurementROIBackup(it, s_measurementRoiPendingBackup);
                    s_measurementRoiPendingBackup.clear();
                    s_measurementRoiModifying = false;
                    s_measurementRoiDrawOwner = -1;
                    ROIState::SetSelectedIndex(-1);
                    ui.saveRecipe();
                }
            }
            else if (s_measurementRoiDrawOwner == inst && UI::IsROIDrawSequenceActive())
            {
                const int step = UI::ROIDrawSequenceStep();
                const int count = UI::ROIDrawSequenceCount();
                ImGui::TextColored(ImVec4(0.35f, 0.8f, 1.0f, 1.0f),
                    "绘制 %d/%d: %s", step + 1, count,
                    RoiTypeDisplayName(UI::gCurrentROIType));
            }
            else if (!it.searchROIs.empty())
            {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                    "已绑定 %zu 个测量 ROI", it.searchROIs.size());

                // 状态文字单独占一行，避免窄侧栏把修改按钮裁掉。
                if (ImGui::BeginTable("##measurement_roi_actions", 2,
                    ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
                {
                    ImGui::TableNextColumn();
                    if (ui.secondaryButtonSized("修改测量 ROI##measurement_roi_edit", -1.0f))
                        StartMeasurementROIDrawing(true);
                    ImGui::TableNextColumn();
                    if (ui.secondaryButtonSized("清除##measurement_roi_clear", -1.0f))
                    {
                        RemoveMeasurementRuntimeRois(it);
                        ui.saveRecipe();
                    }
                    ImGui::EndTable();
                }
            }
            else
            {
                ImGui::TextDisabled("未绑定测量 ROI");
            }

            const bool usesCaliper = it.measureMode == 1 || it.measureMode == 3 ||
                it.measureMode == 4 || it.measureMode == 5;
            if (usesCaliper)
            {
                ui.sectionHeader("卡尺与拟合");
                ImGui::SliderInt("卡尺数量##measure", &it.measureCaliperCount, 1, 128);
                ImGui::InputFloat("搜索长度(px)##measure", &it.measureSearchLength, 1.0f, 5.0f, "%.2f");
                ImGui::InputFloat("投影宽度(px)##measure", &it.measureProjectionWidth, 1.0f, 5.0f, "%.2f");
                ImGui::InputFloat("平滑 Sigma##measure", &it.measureSmoothingSigma, 0.1f, 0.5f, "%.2f");
                ImGui::InputFloat("边缘阈值##measure", &it.measureEdgeThreshold, 1.0f, 5.0f, "%.2f");
                const char* polarities[] = {"任意极性", "暗到明", "明到暗"};
                it.measureEdgePolarity = std::clamp(it.measureEdgePolarity, 0, 2);
                ImGui::Combo("边缘极性##measure", &it.measureEdgePolarity,
                    polarities, IM_ARRAYSIZE(polarities));
                ImGui::Checkbox("亚像素插值##measure", &it.measureSubpixel);
                if (it.measureMode == 1)
                    ImGui::InputFloat("最小边缘间距##measure", &it.measureMinPairDistance, 0.5f, 2.0f, "%.2f");
                if (it.measureMode == 3 || it.measureMode == 5)
                {
                    const char* fitMethods[] = {"最小二乘", "RANSAC"};
                    it.measureFitMethod = std::clamp(it.measureFitMethod, 0, 1);
                    ImGui::Combo("拟合方法##measure", &it.measureFitMethod,
                        fitMethods, IM_ARRAYSIZE(fitMethods));
                    ImGui::InputFloat("内点阈值(px)##measure", &it.measureFitInlierThreshold,
                        0.1f, 0.5f, "%.2f");
                }
                ImGui::SliderInt("最少有效卡尺##measure", &it.measureMinimumValidCalipers, 1, 128);
                ImGui::SliderFloat("最低可信度##measure", &it.measureMinimumConfidence, 0.0f, 1.0f, "%.3f");
                it.measureCaliperCount = std::clamp(it.measureCaliperCount, 1, 128);
                it.measureSearchLength = (std::max)(1.0f, it.measureSearchLength);
                it.measureProjectionWidth = (std::max)(1.0f, it.measureProjectionWidth);
                it.measureSmoothingSigma = (std::max)(0.0f, it.measureSmoothingSigma);
                it.measureEdgeThreshold = (std::max)(0.0f, it.measureEdgeThreshold);
                it.measureMinPairDistance = (std::max)(0.0f, it.measureMinPairDistance);
                it.measureFitInlierThreshold = (std::max)(0.1f, it.measureFitInlierThreshold);
                it.measureMinimumValidCalipers = std::clamp(it.measureMinimumValidCalipers, 1, 128);
            }

            ui.sectionHeader("完整标定");
            ImGui::Checkbox("启用世界坐标(mm)##measure", &it.measureCalibration.enabled);
            ImGui::InputFloat("参考像素##measure", &it.measureCalibrationPixels, 1.0f, 10.0f, "%.3f");
            ImGui::InputFloat("实际长度(mm)##measure", &it.measureCalibrationMm, 0.1f, 1.0f, "%.4f");
            if (ui.secondaryButton("计算 mm/px") && it.measureCalibrationPixels > 0.0f)
            {
                it.measureMmPerPixel = it.measureCalibrationMm / it.measureCalibrationPixels;
                it.measureCalibration.scaleX = it.measureMmPerPixel;
                it.measureCalibration.scaleY = it.measureMmPerPixel;
                it.measureCalibration.enabled = true;
            }
            ImGui::InputDouble("X 比例(mm/px)##measure", &it.measureCalibration.scaleX, 0.0001, 0.001, "%.8f");
            ImGui::InputDouble("Y 比例(mm/px)##measure", &it.measureCalibration.scaleY, 0.0001, 0.001, "%.8f");
            ImGui::InputDouble("像素原点 X##measure", &it.measureCalibration.pixelOrigin.x, 0.1, 1.0, "%.4f");
            ImGui::InputDouble("像素原点 Y##measure", &it.measureCalibration.pixelOrigin.y, 0.1, 1.0, "%.4f");
            ImGui::InputDouble("世界原点 X(mm)##measure", &it.measureCalibration.worldOrigin.x, 0.1, 1.0, "%.4f");
            ImGui::InputDouble("世界原点 Y(mm)##measure", &it.measureCalibration.worldOrigin.y, 0.1, 1.0, "%.4f");

            ui.sectionHeader("多点标定向导");
            ImGui::SeparatorText("棋盘格镜头标定");
            ImGui::InputInt("内角点列数##chessboard", &it.measureChessboardColumns);
            ImGui::InputInt("内角点行数##chessboard", &it.measureChessboardRows);
            ImGui::InputFloat("方格尺寸(mm)##chessboard",
                &it.measureChessboardSquareSize, 0.1f, 1.0f, "%.4f");
            it.measureChessboardColumns = std::clamp(it.measureChessboardColumns, 2, 64);
            it.measureChessboardRows = std::clamp(it.measureChessboardRows, 2, 64);
            it.measureChessboardSquareSize = (std::max)(0.0001f,
                it.measureChessboardSquareSize);
            ImGui::InputFloat("RMS验收上限(px)##chessboard",
                &it.measureCalibrationRmsAcceptance, 0.01f, 0.1f, "%.3f");
            ImGui::InputFloat("最大误差上限(px)##chessboard",
                &it.measureCalibrationMaxAcceptance, 0.01f, 0.1f, "%.3f");
            it.measureCalibrationRmsAcceptance = (std::max)(0.0f,
                it.measureCalibrationRmsAcceptance);
            it.measureCalibrationMaxAcceptance = (std::max)(0.0f,
                it.measureCalibrationMaxAcceptance);
            if (ImGui::BeginTable("##chessboard_capture_actions", 2,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
            {
                ImGui::TableNextColumn();
                if (ui.secondaryButtonSized("采集当前标定图##chessboard_capture", -1.0f))
                {
                    const cv::Mat& image = ImageState::Current();
                    if (!image.empty())
                        it.measureChessboardImages.push_back(image.clone());
                }
                ImGui::TableNextColumn();
                if (ui.secondaryButtonSized("清空标定图##chessboard_clear", -1.0f))
                {
                    it.measureChessboardImages.clear();
                    it.measureChessboardErrors.clear();
                    it.measureChessboardSuccessfulImages = 0;
                }
                ImGui::EndTable();
            }
            ImGui::TextDisabled("已采集 %zu 张；建议 10-20 张不同位置和倾角",
                it.measureChessboardImages.size());
            if (ui.secondaryButton("执行棋盘格标定##chessboard_fit"))
            {
                const CalibrationFitResult fit = CalibrationFitter::FitChessboard(
                    it.measureChessboardImages,
                    cv::Size(it.measureChessboardColumns, it.measureChessboardRows),
                    it.measureChessboardSquareSize, it.measureCalibration,
                    it.measureCalibrationRmsAcceptance,
                    it.measureCalibrationMaxAcceptance);
                it.measureCalibrationFitMessage = fit.message;
                if (fit.success)
                {
                    it.measureCalibration = fit.model;
                    it.measureCalibrationRmsError = fit.rmsError;
                    it.measureCalibrationMaxError = fit.maxError;
                    it.measureChessboardErrors = fit.residuals;
                    it.measureChessboardSuccessfulImages = static_cast<int>(
                        fit.successfulImageCount);
                    ui.saveRecipe();
                }
            }
            if (!it.measureChessboardErrors.empty())
            {
                const bool accepted = it.measureCalibrationRmsError <=
                        it.measureCalibrationRmsAcceptance &&
                    it.measureCalibrationMaxError <=
                        it.measureCalibrationMaxAcceptance;
                ImGui::TextColored(accepted ? ImVec4(0.25f, 0.9f, 0.35f, 1.0f)
                                            : ImVec4(1.0f, 0.3f, 0.25f, 1.0f),
                    "%s  成功 %d/%zu  RMS %.4f px  最大 %.4f px",
                    accepted ? "PASS" : "FAIL", it.measureChessboardSuccessfulImages,
                    it.measureChessboardImages.size(), it.measureCalibrationRmsError,
                    it.measureCalibrationMaxError);
                ImGui::Text("重投影误差（像素）");
                const double maximum = (std::max)(1.0,
                    *std::max_element(it.measureChessboardErrors.begin(),
                                      it.measureChessboardErrors.end()));
                for (std::size_t errorIndex = 0;
                    errorIndex < it.measureChessboardErrors.size(); ++errorIndex)
                {
                    char overlay[64];
                    snprintf(overlay, sizeof(overlay), "图 %zu: %.4f px",
                        errorIndex + 1, it.measureChessboardErrors[errorIndex]);
                    ImGui::ProgressBar(static_cast<float>(
                        it.measureChessboardErrors[errorIndex] / maximum),
                        ImVec2(-1.0f, 0.0f), overlay);
                }
                if (ui.secondaryButton("导出现场验收报告##chessboard_report"))
                {
                    const std::string path = SaveFileDialogWithFilter(
                        L"标定验收报告 (*.json;*.csv)\0*.json;*.csv\0JSON (*.json)\0*.json\0CSV (*.csv)\0*.csv\0",
                        L"导出标定现场验收报告", L"json");
                    if (!path.empty() && CalibrationFitter::SaveAcceptanceReport(
                        path.c_str(), it.measureCalibration,
                        it.measureChessboardImages.size(),
                        static_cast<std::size_t>(it.measureChessboardSuccessfulImages),
                        it.measureChessboardErrors, it.measureCalibrationRmsError,
                        it.measureCalibrationMaxError,
                        it.measureCalibrationRmsAcceptance,
                        it.measureCalibrationMaxAcceptance))
                        LogSystem::Add(LOG_INFO, "标定验收报告已导出: %s", path.c_str());
                    else if (!path.empty())
                        LogSystem::Add(LOG_ERROR, "标定验收报告导出失败: %s", path.c_str());
                }
            }

            const CalibrationFitResult calibrationEvaluation = CalibrationFitter::Evaluate(
                it.measureCalibration, it.measureCalibrationSamples);
            int removeCalibrationSample = -1;
            const float calibrationTableHeight = (std::min)(190.0f,
                ImGui::GetTextLineHeightWithSpacing() *
                    (static_cast<float>(it.measureCalibrationSamples.size()) + 2.5f));
            const float calibrationTableMinWidth = ImGui::GetFontSize() * 34.0f;
            if (ImGui::BeginTable("##measure_calibration_samples", 6,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_SizingStretchProp,
                ImVec2(0.0f, (std::max)(70.0f, calibrationTableHeight)),
                calibrationTableMinWidth))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 28.0f);
                ImGui::TableSetupColumn("像素 X");
                ImGui::TableSetupColumn("像素 Y");
                ImGui::TableSetupColumn("世界 X");
                ImGui::TableSetupColumn("世界 Y");
                ImGui::TableSetupColumn("残差", ImGuiTableColumnFlags_WidthFixed, 58.0f);
                ImGui::TableHeadersRow();
                for (size_t sampleIndex = 0;
                    sampleIndex < it.measureCalibrationSamples.size(); ++sampleIndex)
                {
                    CalibrationSample& sample = it.measureCalibrationSamples[sampleIndex];
                    ImGui::PushID(static_cast<int>(sampleIndex));
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    if (ImGui::SmallButton("X"))
                        removeCalibrationSample = static_cast<int>(sampleIndex);
                    ImGui::SetItemTooltip("删除标定点 %zu", sampleIndex + 1);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputDouble("##pixel_x", &sample.pixel.x, 0.1, 1.0, "%.3f");
                    ImGui::TableSetColumnIndex(2);
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputDouble("##pixel_y", &sample.pixel.y, 0.1, 1.0, "%.3f");
                    ImGui::TableSetColumnIndex(3);
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputDouble("##world_x", &sample.world.x, 0.1, 1.0, "%.3f");
                    ImGui::TableSetColumnIndex(4);
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputDouble("##world_y", &sample.world.y, 0.1, 1.0, "%.3f");
                    ImGui::TableSetColumnIndex(5);
                    if (sampleIndex < calibrationEvaluation.residuals.size())
                        ImGui::Text("%.4f", calibrationEvaluation.residuals[sampleIndex]);
                    else
                        ImGui::TextDisabled("-");
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (removeCalibrationSample >= 0)
                it.measureCalibrationSamples.erase(it.measureCalibrationSamples.begin() +
                    static_cast<std::ptrdiff_t>(removeCalibrationSample));
            if (ui.secondaryButton("添加标定点##measure_calibration_add"))
                it.measureCalibrationSamples.push_back({});
            if (ImGui::BeginTable("##measure_calibration_fit_actions", 2,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
            {
                ImGui::TableNextColumn();
                if (ui.secondaryButtonSized("拟合X/Y比例##measure_calibration_scale", -1.0f))
                {
                    const CalibrationFitResult fit = CalibrationFitter::FitScale(it.measureCalibrationSamples);
                    it.measureCalibrationFitMessage = fit.message;
                    if (fit.success)
                    {
                        it.measureCalibration = fit.model;
                        it.measureCalibrationRmsError = fit.rmsError;
                        it.measureCalibrationMaxError = fit.maxError;
                        ui.saveRecipe();
                    }
                }
                ImGui::TableNextColumn();
                if (ui.secondaryButtonSized("拟合透视##measure_calibration_h", -1.0f))
                {
                    const CalibrationFitResult fit = CalibrationFitter::FitHomography(it.measureCalibrationSamples);
                    it.measureCalibrationFitMessage = fit.message;
                    if (fit.success)
                    {
                        it.measureCalibration = fit.model;
                        it.measureCalibrationRmsError = fit.rmsError;
                        it.measureCalibrationMaxError = fit.maxError;
                        ui.saveRecipe();
                    }
                }
                ImGui::EndTable();
            }
            if (!it.measureCalibrationFitMessage.empty())
            {
                ImGui::TextDisabled("%s | RMS %.6f | 最大 %.6f",
                    it.measureCalibrationFitMessage.c_str(),
                    it.measureCalibrationRmsError,
                    it.measureCalibrationMaxError);
            }
            if (ImGui::BeginTable("##measure_calibration_file_actions", 2,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
            {
                ImGui::TableNextColumn();
                if (ui.secondaryButtonSized("导入标定文件##measure_calibration_import", -1.0f))
                {
                    const std::string path = OpenFileDialogWithFilter(
                        L"标定文件 (*.json)\0*.json\0所有文件 (*.*)\0*.*\0", L"导入标定文件");
                    CalibrationModel loadedModel;
                    std::vector<CalibrationSample> loadedSamples;
                    if (!path.empty() && CalibrationFitter::LoadDocument(
                        path.c_str(), loadedModel, loadedSamples))
                    {
                        it.measureCalibration = loadedModel;
                        it.measureCalibrationSamples = std::move(loadedSamples);
                        const CalibrationFitResult evaluation = CalibrationFitter::Evaluate(
                            it.measureCalibration, it.measureCalibrationSamples);
                        it.measureCalibrationRmsError = evaluation.rmsError;
                        it.measureCalibrationMaxError = evaluation.maxError;
                        it.measureCalibrationFitMessage = "标定文件已导入";
                        ui.saveRecipe();
                        LogSystem::Add(LOG_INFO, "工业测量: 已导入标定文件 %s", path.c_str());
                    }
                    else if (!path.empty())
                    {
                        LogSystem::Add(LOG_ERROR, "工业测量: 标定文件导入失败 %s", path.c_str());
                    }
                }
                ImGui::TableNextColumn();
                if (ui.secondaryButtonSized("导出标定文件##measure_calibration_export", -1.0f))
                {
                    const std::string path = SaveFileDialogWithFilter(
                        L"标定文件 (*.json)\0*.json\0所有文件 (*.*)\0*.*\0",
                        L"导出标定文件", L"json");
                    if (!path.empty() && CalibrationFitter::SaveDocument(path.c_str(),
                        it.measureCalibration, it.measureCalibrationSamples))
                    {
                        LogSystem::Add(LOG_INFO, "工业测量: 已导出标定文件 %s", path.c_str());
                    }
                    else if (!path.empty())
                    {
                        LogSystem::Add(LOG_ERROR, "工业测量: 标定文件导出失败 %s", path.c_str());
                    }
                }
                ImGui::EndTable();
            }

            ImGui::Checkbox("启用透视矩阵##measure", &it.measureCalibration.homographyEnabled);
            if (it.measureCalibration.homographyEnabled && ImGui::TreeNode("3x3 像素到世界矩阵##measure"))
            {
                if (ImGui::BeginTable("##measure_homography_matrix", 3,
                    ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
                {
                    for (int row = 0; row < 3; ++row)
                    {
                        ImGui::TableNextRow();
                        for (int column = 0; column < 3; ++column)
                        {
                            ImGui::TableSetColumnIndex(column);
                            char matrixLabel[16] = {};
                            snprintf(matrixLabel, sizeof(matrixLabel), "H%d%d", row, column);
                            ImGui::TextDisabled("%s", matrixLabel);
                            ImGui::SetNextItemWidth(-1.0f);
                            ImGui::PushID(row * 3 + column);
                            ImGui::InputDouble("##matrix_value",
                                &it.measureCalibration.pixelToWorldHomography(row, column),
                                0.0001, 0.001, "%.8f");
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndTable();
                }
                ImGui::TreePop();
            }

            ImGui::Checkbox("启用镜头畸变校正##measure", &it.measureCalibration.distortionEnabled);
            if (it.measureCalibration.distortionEnabled && ImGui::TreeNode("相机内参与畸变##measure"))
            {
                ImGui::InputDouble("fx##measure", &it.measureCalibration.fx, 1.0, 10.0, "%.6f");
                ImGui::InputDouble("fy##measure", &it.measureCalibration.fy, 1.0, 10.0, "%.6f");
                ImGui::InputDouble("cx##measure", &it.measureCalibration.cx, 1.0, 10.0, "%.6f");
                ImGui::InputDouble("cy##measure", &it.measureCalibration.cy, 1.0, 10.0, "%.6f");
                ImGui::InputDouble("k1##measure", &it.measureCalibration.k1, 0.0001, 0.001, "%.8f");
                ImGui::InputDouble("k2##measure", &it.measureCalibration.k2, 0.0001, 0.001, "%.8f");
                ImGui::InputDouble("p1##measure", &it.measureCalibration.p1, 0.0001, 0.001, "%.8f");
                ImGui::InputDouble("p2##measure", &it.measureCalibration.p2, 0.0001, 0.001, "%.8f");
                ImGui::InputDouble("k3##measure", &it.measureCalibration.k3, 0.0001, 0.001, "%.8f");
                ImGui::TreePop();
            }
            it.measureCalibration.scaleX = (std::max)(1.0e-12, it.measureCalibration.scaleX);
            it.measureCalibration.scaleY = (std::max)(1.0e-12, it.measureCalibration.scaleY);

            ui.sectionHeader("公差");
            ImGui::Checkbox("启用公差##measure", &it.measureToleranceEnabled);
            if (it.measureToleranceEnabled)
            {
                ImGui::InputFloat("标称值##measure", &it.measureNominal, 0.1f, 1.0f, "%.4f");
                ImGui::InputFloat("下偏差##measure", &it.measureToleranceMinus, 0.01f, 0.1f, "%.4f");
                ImGui::InputFloat("上偏差##measure", &it.measureTolerancePlus, 0.01f, 0.1f, "%.4f");
                it.measureToleranceMinus = (std::max)(0.0f, it.measureToleranceMinus);
                it.measureTolerancePlus = (std::max)(0.0f, it.measureTolerancePlus);
            }

            ui.endCard();
        };

}
