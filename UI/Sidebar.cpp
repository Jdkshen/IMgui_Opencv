#include "Sidebar.h"
#include "DockSpaceHost.h"
#include "ImageViewer.h"
#include "ROIManager.h"
#include "../Core/ThemeManager.h"
#include "../Core/ROIState.h"
#include "../include/imgui/imgui.h"
#include "../Log/LogSystem.h"

namespace UI
{
    void ShowSidebar()
    {
        if (!g_ShowSidebar)
            return;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 5));
        ImGui::Begin("侧边栏", &g_ShowSidebar);

        const bool isDark = (g_CurrentTheme == 0);
        auto SectionTitle = [isDark](const char* label)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, isDark
                ? ImVec4(0.42f, 0.78f, 0.84f, 1.0f)
                : ImVec4(0.05f, 0.39f, 0.46f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Separator, isDark
                ? ImVec4(0.18f, 0.36f, 0.40f, 1.0f)
                : ImVec4(0.48f, 0.67f, 0.70f, 1.0f));
            ImGui::SeparatorText(label);
            ImGui::PopStyleColor(2);
        };

        ImGui::TextColored(isDark
            ? ImVec4(0.88f, 0.91f, 0.94f, 1.0f)
            : ImVec4(0.10f, 0.16f, 0.19f, 1.0f), "控制面板");

        // =========================
        // ROI 类型切换
        // =========================
        SectionTitle("ROI 绘制");
        const char *kROITypeNames[] = {"矩形(0)", "点(1)", "线段(2)", "圆(3)", "多边形(4)"};
        ImGui::TextDisabled("类型");
        ImGui::PushItemWidth(-1);
        if (ImGui::BeginCombo("##ROIType", kROITypeNames[gCurrentROIType]))
        {
            for (int i = 0; i < ROI_TYPE_COUNT; i++)
            {
                bool isSelected = (gCurrentROIType == i);
                ImU32 col = GetROIColor(i, false);
                ImVec4 col4 = ImGui::ColorConvertU32ToFloat4(col);
                ImGui::PushStyleColor(ImGuiCol_Text, col4);
                if (ImGui::Selectable(kROITypeNames[i], isSelected))
                {
                    CancelROIDrawSequence();
                    gCurrentROIType = i;
                }
                ImGui::PopStyleColor();
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();
        ImGui::SetItemTooltip("右键画框时将创建此类型的ROI");

        ROI* selectedROI = ROIState::MutableAt(ROIState::SelectedIndex());
        if (selectedROI && selectedROI->type == ROI_TYPE_RECT)
        {
            ImGui::TextDisabled("矩形角度");
            ImGui::SetNextItemWidth(-52.0f);
            if (ImGui::DragFloat("##roi_angle", &selectedROI->angle, 0.1f,
                                 -180.0f, 180.0f, "%.2f deg"))
            {
                while (selectedROI->angle > 180.0f) selectedROI->angle -= 360.0f;
                while (selectedROI->angle <= -180.0f) selectedROI->angle += 360.0f;
            }
            ImGui::SameLine();
            if (ImGui::Button("归零##roi_angle_reset"))
                selectedROI->angle = 0.0f;
        }

        if (ImGui::Button("清除当前类型 ROI", ImVec2(-1, 0)))
        {
            auto& rois = ROIState::Items();
            rois.erase(
                std::remove_if(rois.begin(), rois.end(),
                               [](const ROI &r)
                               { return r.type == gCurrentROIType; }),
                rois.end());
            ROIState::SetSelectedIndex(-1);
            gActiveHandle = HANDLE_NONE;
        }

        // 快捷操作
        SectionTitle("图像操作");
        if (ImGui::Button("打印 ROI 信息", ImVec2(-1, 0)))
            PrintROIToLog();
        if (ImGui::Button("清理图片", ImVec2(-1, 0)))
            ClearImage();

        // 自定义日志输入
        SectionTitle("调试日志");
        static char inputBuf[256] = {0};
        ImGui::TextDisabled("自定义日志输入");
        ImGui::PushItemWidth(-1);
        ImGui::InputText("##log_input", inputBuf, sizeof(inputBuf));
        ImGui::PopItemWidth();
        if (ImGui::Button("发送到日志", ImVec2(-1, 28)))
        {
            if (strlen(inputBuf) > 0)
            {
                LogSystem::Add(LOG_INFO, "自定义: %s", inputBuf);
                inputBuf[0] = '\0';
            }
        }

        ImGui::End();
        ImGui::PopStyleVar(2);
    }

} // namespace UI
