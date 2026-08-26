#include "DetectionToolPanels.h"

#include "../GeometryDrawEditor.h"
#include "../../Algorithm/ContourDetector.h"
#include "../../Algorithm/LineDetector.h"
#include "../../Core/BarcodeTypes.h"
#include "../../Core/GeometryPrimitive.h"
#include "../../Core/ToolAssetService.h"
#include "../../Log/LogSystem.h"
#include "../../include/imgui/imgui.h"

#include <algorithm>
#include <cstdint>

void RegisterDetectionToolPanels(
    std::unordered_map<int, ToolUIFn>& panels,
    const ToolPanelContext& ui)
{
    panels[0] = [ui](ToolInstance& tool, int instanceIndex)
    {
        ui.beginCard("边缘检测");
        if (ui.secondaryButton("重置参数"))
        {
            tool.cannyLow = 50;
            tool.cannyHigh = 150;
            tool.edgeUseGray = false;
        }
        if (ui.primaryButton("执行边缘检测") && ui.runTool(instanceIndex))
        {
            LogSystem::Add(LOG_INFO, "Canny(%d,%d)",
                tool.cannyLow, tool.cannyHigh);
        }
        ui.drawSearchROI(tool, instanceIndex);
        ui.sectionHeader("参数");
        ImGui::SliderInt("Canny低阈值", &tool.cannyLow, 0, 255);
        ImGui::SliderInt("Canny高阈值", &tool.cannyHigh, 0, 255);
        ImGui::Checkbox("转为灰度", &tool.edgeUseGray);
        ui.endCard();
    };

    panels[5] = [ui](ToolInstance& tool, int instanceIndex)
    {
        ui.beginCard("轮廓分析");
        if (ui.secondaryButton("重置参数"))
        {
            tool.cntUseGray = true;
            tool.cntBlurSize = 5;
            tool.cntThreshMode = 0;
            tool.cntThreshValue = 128;
            tool.cntAdaptBlock = 11;
            tool.cntInvert = false;
            tool.cntRetrMode = 0;
            tool.cntApproxMethod = 1;
            tool.cntMinArea = 100;
            tool.cntMaxContours = 500;
            tool.cntFilterConvex = false;
            tool.cntApproxEps = 0.02f;
            tool.cntLineThick = 2;
            tool.showResultLabels = true;
            tool.cntFillContours = false;
            tool.cntNormalizeDirection = true;
            tool.cntSubpixelBoundary = true;
            tool.cntMatchROI = false;
            tool.cntMatchThresh = 0.1f;
        }
        if (ContourDetector::g_ContourTimeMs > 0)
        {
            ImGui::TextDisabled("上次: %d个 %.3fms",
                ContourDetector::g_ContourCount,
                ContourDetector::g_ContourTimeMs);
        }
        if (ui.primaryButton("执行轮廓分析"))
            ui.runTool(instanceIndex);
        ui.drawSearchROI(tool, instanceIndex);
        ui.sectionHeader("参数");
        if (ImGui::BeginTable("##contour_preprocess_options", 2,
            ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
        {
            ImGui::TableNextColumn();
            ImGui::Checkbox("灰度##c", &tool.cntUseGray);
            ImGui::TableNextColumn();
            ImGui::Checkbox("反色##c", &tool.cntInvert);
            ImGui::EndTable();
        }
        ImGui::SliderInt("模糊核##c", &tool.cntBlurSize, 0, 20);
        const char* thresholdModes[] = {"OTSU", "固定", "自适应"};
        ImGui::Combo("二值化##c", &tool.cntThreshMode, thresholdModes, 3);
        if (tool.cntThreshMode == 1)
            ImGui::SliderInt("阈值##c", &tool.cntThreshValue, 0, 255);
        else if (tool.cntThreshMode == 2)
            ImGui::SliderInt("块大小##c", &tool.cntAdaptBlock, 3, 51);
        const char* retrievalModes[] = {"EXTERNAL", "LIST", "TREE"};
        const char* approximationModes[] = {"NONE", "SIMPLE", "Teh-Chin"};
        ImGui::Combo("检索##c", &tool.cntRetrMode, retrievalModes, 3);
        ImGui::Combo("近似##c", &tool.cntApproxMethod, approximationModes, 3);
        ImGui::InputFloat("最小面积##c", &tool.cntMinArea, 10, 100, "%.0f");
        ImGui::SliderInt("最多##c", &tool.cntMaxContours, 10, 2000);
        ImGui::Checkbox("仅凸包##c", &tool.cntFilterConvex);
        ImGui::SliderFloat("精度##c", &tool.cntApproxEps, 0.005f, 0.05f, "%.3f");
        ImGui::SliderInt("线宽##c", &tool.cntLineThick, 1, 5);
        ImGui::Checkbox("填充##c", &tool.cntFillContours);
        ui.sectionHeader("高级");
        if (ImGui::BeginTable("##contour_advanced_options", 2,
            ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
        {
            ImGui::TableNextColumn();
            ImGui::Checkbox("统一轮廓方向##c", &tool.cntNormalizeDirection);
            ImGui::TableNextColumn();
            ImGui::Checkbox("亚像素边界##c", &tool.cntSubpixelBoundary);
            ImGui::EndTable();
        }
        ImGui::Checkbox("ROI模板匹配##c", &tool.cntMatchROI);
        if (tool.cntMatchROI)
            ImGui::SliderFloat("匹配阈值##c", &tool.cntMatchThresh,
                0.01f, 0.5f, "%.3f");
        ui.endCard();
    };

    panels[7] = [ui](ToolInstance& tool, int instanceIndex)
    {
        ui.beginCard("直线检测");
        if (ui.secondaryButton("重置参数"))
        {
            tool.lineCannyLow = 50;
            tool.lineCannyHigh = 150;
            tool.lineMinLength = 100;
            tool.lineMaxGap = 20;
            tool.lineMinAngle = 0;
            tool.lineMaxAngle = 180;
            tool.lineThickness = 2;
            tool.lineMaxLines = 1;
            tool.showResultLabels = true;
            tool.lineUseROI = false;
        }
        if (LineDetector::g_LineTimeMs > 0)
        {
            ImGui::TextDisabled("上次: %d条 %.3fms",
                LineDetector::g_LineCount, LineDetector::g_LineTimeMs);
        }
        if (ui.primaryButton("执行直线检测"))
            ui.runTool(instanceIndex);
        ui.drawSearchROI(tool, instanceIndex);
        ui.sectionHeader("参数");
        ImGui::SliderInt("Canny低##l", &tool.lineCannyLow, 0, 255);
        ImGui::SliderInt("Canny高##l", &tool.lineCannyHigh, 0, 255);
        ImGui::SliderFloat("最小线长##l", &tool.lineMinLength, 10, 500);
        ImGui::SliderFloat("最大间隙##l", &tool.lineMaxGap, 5, 100);
        ImGui::SliderFloat("最小角度##l", &tool.lineMinAngle, 0, 180);
        ImGui::SliderFloat("最大角度##l", &tool.lineMaxAngle, 0, 180);
        ImGui::SliderInt("线宽##l", &tool.lineThickness, 1, 5);
        ImGui::SliderInt("最多条数##l", &tool.lineMaxLines, 1, 100);
        ui.endCard();
    };

    panels[13] = [ui](ToolInstance& tool, int instanceIndex)
    {
        const auto resetModelPaths = [&tool]()
        {
            tool.ocrDetParamPath = "models\\ppocrv6\\PP_OCRv6_tiny_det.ncnn.param";
            tool.ocrDetModelPath = "models\\ppocrv6\\PP_OCRv6_tiny_det.ncnn.bin";
            tool.ocrRecParamPath = "models\\ppocrv6\\PP_OCRv6_tiny_rec.ncnn.param";
            tool.ocrRecModelPath = "models\\ppocrv6\\PP_OCRv6_tiny_rec.ncnn.bin";
            tool.ocrDictionaryPath = "models\\ppocrv6\\ppocr_keys_v6_tiny.txt";
        };
        if (tool.ocrDetParamPath.empty() || tool.ocrDetModelPath.empty() ||
            tool.ocrRecParamPath.empty() || tool.ocrRecModelPath.empty() ||
            tool.ocrDictionaryPath.empty())
        {
            resetModelPaths();
        }
        ui.beginCard("文字识别");
        if (ui.secondaryButton("重置参数"))
        {
            resetModelPaths();
            tool.ocrMinConfidence = 0.30f;
            tool.ocrMaxItems = 8;
            tool.ocrInputSize = 512;
            tool.ocrMaxCandidates = 220;
            tool.ocrMinBoxArea = 0;
            tool.ocrMinBoxHeight = 0;
            tool.ocrRoiPadding = 24;
            tool.ocrFastMode = true;
            tool.ocrDetectOnly = false;
            tool.ocrUseROI = true;
        }
        if (ui.primaryButton("执行文字识别"))
            ui.runTool(instanceIndex);
        ui.drawSearchROI(tool, instanceIndex);
        ui.sectionHeader("参数");
        ImGui::SliderFloat("置信度##ocr", &tool.ocrMinConfidence, 0.01f, 1.0f, "%.2f");
        ImGui::SliderInt("最多文本##ocr", &tool.ocrMaxItems, 1, 1000);
        ImGui::TextDisabled("提示: 文本数越大，OCR耗时越高");
        ImGui::SliderInt("最大候选##ocr", &tool.ocrMaxCandidates, 1, 2000);
        ImGui::SliderInt("输入尺寸##ocr", &tool.ocrInputSize, 320, 1536);
        ImGui::SliderInt("最小框面积##ocr", &tool.ocrMinBoxArea, 0, 20000);
        ImGui::SliderInt("最小框高度##ocr", &tool.ocrMinBoxHeight, 0, 120);
        ImGui::SliderInt("ROI扩边##ocr", &tool.ocrRoiPadding, 0, 256);
        if (ImGui::BeginTable("##ocr_options", 2,
            ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
        {
            ImGui::TableNextColumn();
            ImGui::Checkbox("快速模式##ocr", &tool.ocrFastMode);
            ImGui::TableNextColumn();
            ImGui::Checkbox("只检测##ocr", &tool.ocrDetectOnly);
            ImGui::TableNextColumn();
            ImGui::Checkbox("使用ROI##ocr", &tool.ocrUseROI);
            ImGui::EndTable();
        }
        ImGui::TextDisabled("模型: 默认 PP-OCRv6 tiny");
        ImGui::TextDisabled("状态: NCNN OCR接口已接入，未启用依赖时会提示");
        ui.endCard();
    };

    panels[14] = [ui](ToolInstance& tool, int instanceIndex)
    {
        ui.beginCard("二维码/条码识别");
        if (ui.secondaryButton("重置参数"))
        {
            tool.qrUseROI = true;
            tool.qrDetectMulti = true;
            tool.qrEnhance = true;
            tool.qrMinSize = 24;
            tool.showResultLabels = true;
            tool.qrEngine = 0;
            tool.qrFormatMask = BarcodeFormatAll;
            tool.qrFilterDuplicates = true;
        }
        if (ui.primaryButton("执行二维码/条码识别"))
            ui.runTool(instanceIndex);
        ui.drawSearchROI(tool, instanceIndex);
        ui.sectionHeader("参数");
        const char* engines[] = {"自动(ZXing优先)", "OpenCV", "ZXing-cpp"};
        tool.qrEngine = std::clamp(tool.qrEngine, 0, 2);
        ImGui::Combo("识别引擎##qr", &tool.qrEngine, engines, IM_ARRAYSIZE(engines));
        if (ImGui::BeginTable("##qr_options", 2,
            ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
        {
            ImGui::TableNextColumn();
            ImGui::Checkbox("使用ROI##qr", &tool.qrUseROI);
            ImGui::TableNextColumn();
            ImGui::Checkbox("识别多个##qr", &tool.qrDetectMulti);
            ImGui::TableNextColumn();
            ImGui::Checkbox("增强识别##qr", &tool.qrEnhance);
            ImGui::TableNextColumn();
            ImGui::Checkbox("过滤重复码##qr", &tool.qrFilterDuplicates);
            ImGui::EndTable();
        }
        ImGui::SliderInt("最小尺寸##qr", &tool.qrMinSize, 8, 512);
        ImGui::TextDisabled("码制过滤");
        const auto formatCheckbox = [&tool](const char* label, std::uint32_t flag)
        {
            bool selected = (tool.qrFormatMask & flag) != 0;
            if (ImGui::Checkbox(label, &selected))
            {
                if (selected)
                    tool.qrFormatMask |= flag;
                else
                    tool.qrFormatMask &= ~flag;
            }
        };
        if (ImGui::BeginTable("##qr_formats", 2,
            ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
        {
            ImGui::TableNextColumn();
            formatCheckbox("QR Code##qrFmt", BarcodeFormatQR);
            ImGui::TableNextColumn();
            formatCheckbox("Code128##qrFmt", BarcodeFormatCode128);
            ImGui::TableNextColumn();
            formatCheckbox("EAN##qrFmt", BarcodeFormatEAN);
            ImGui::TableNextColumn();
            formatCheckbox("Data Matrix##qrFmt", BarcodeFormatDataMatrix);
            ImGui::TableNextColumn();
            formatCheckbox("PDF417##qrFmt", BarcodeFormatPDF417);
            ImGui::EndTable();
        }
        if (tool.qrFormatMask == 0)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f),
                "请至少选择一种码制");
        }
        if (tool.qrEngine == 1 && (tool.qrFormatMask & ~BarcodeFormatQR) != 0)
            ImGui::TextDisabled("OpenCV 引擎仅支持 QR，其他码制请选自动或 ZXing-cpp");
        ImGui::TextDisabled("解码内容条件在卡片公共“合格判定”中配置");
        ImGui::TextDisabled("文字显示由卡片顶部的“结果标签”统一控制");
        ui.endCard();
    };

    panels[16] = [ui](ToolInstance& tool, int instanceIndex)
    {
        ui.beginCard("图像差分");
        if (ui.primaryButton("执行图像差分"))
            ui.runTool(instanceIndex);
        if (tool.differenceReferenceImage.empty())
        {
            ImGui::TextDisabled("尚未设置参考图");
            if (ui.secondaryButton("从当前图抓取参考图") &&
                ToolAssetService::CaptureCurrentImage(
                    tool, ToolAssetKind::DifferenceReference).success)
            {
                ui.markRecipeAssetsDirty();
            }
        }
        else
        {
            ImGui::Text("参考图: %dx%d", tool.differenceReferenceImage.cols,
                tool.differenceReferenceImage.rows);
            if (ui.secondaryButton("更新参考图") &&
                ToolAssetService::CaptureCurrentImage(
                    tool, ToolAssetKind::DifferenceReference).success)
            {
                ui.markRecipeAssetsDirty();
            }
            if (ui.secondaryButton("清除参考图"))
            {
                ToolAssetService::ClearAsset(
                    tool, ToolAssetKind::DifferenceReference);
                ui.markRecipeAssetsDirty();
            }
        }
        ui.drawSearchROI(tool, instanceIndex);
        ui.sectionHeader("参数");
        ImGui::SliderInt("差异阈值", &tool.differenceThreshold, 0, 255);
        ImGui::SliderInt("最小差异面积", &tool.differenceMinArea, 1, 100000);
        ImGui::SliderInt("预模糊", &tool.differenceBlurSize, 0, 15);
        ImGui::SliderInt("形态学核", &tool.differenceMorphKernelSize, 1, 15);
        ImGui::SliderInt("形态学迭代", &tool.differenceMorphIterations, 1, 10);
        ImGui::Checkbox("反相差分", &tool.differenceInvert);
        ui.endCard();
    };

    panels[17] = [ui](ToolInstance& tool, int instanceIndex)
    {
        ui.beginCard("几何绘制");
        if (ui.secondaryButton("重置图形"))
        {
            tool.geometryDrawType = static_cast<int>(GeometryPrimitiveType::Line);
            tool.geometryItems.clear();
            UI::GeometryDrawEditor::Cancel();
            ui.saveRecipe();
        }
        if (ui.primaryButton("执行几何绘制"))
            ui.runTool(instanceIndex);
        ui.sectionHeader("图形编辑");
        if (UI::GeometryDrawEditor::DrawToolPanel(tool, instanceIndex))
            ui.saveRecipe();
        ui.endCard();
    };
}
