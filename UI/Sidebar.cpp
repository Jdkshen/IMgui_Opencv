#include "../Windows_imgui.h"
#include "Sidebar.h"
#include "ImageViewer.h"
#include "ROIManager.h"
#include "../Log/LogSystem.h"

namespace UI
{

void ShowSidebar()
{
    if (!g_ShowSidebar) return;

    ImGui::Begin("侧边栏", &g_ShowSidebar);

    ImGui::Text("控制面板");
    ImGui::Separator();

    // =========================
    // ROI 类型切换
    // =========================
    const char* kROITypeNames[] = { "通用(0)", "模板(1)", "识别(2)", "类型3", "类型4" };
    if (ImGui::BeginCombo("##ROIType", kROITypeNames[gCurrentROIType]))
    {
        for (int i = 0; i < ROI_TYPE_COUNT; i++)
        {
            bool isSelected = (gCurrentROIType == i);
            ImU32 col = GetROIColor(i, false);
            ImVec4 col4 = ImGui::ColorConvertU32ToFloat4(col);
            ImGui::PushStyleColor(ImGuiCol_Text, col4);
            if (ImGui::Selectable(kROITypeNames[i], isSelected))
                gCurrentROIType = i;
            ImGui::PopStyleColor();
            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("右键画框时将创建此类型的ROI");

    ImGui::SameLine();
    if (ImGui::SmallButton("清除本类"))
    {
        gROIs.erase(
            std::remove_if(gROIs.begin(), gROIs.end(),
                [](const ROI& r) { return r.type == gCurrentROIType; }),
            gROIs.end());
        gSelectedROI = -1;
        gActiveHandle = HANDLE_NONE;
    }

    ImGui::Spacing();

    // 快捷操作
    if (ImGui::Button("打印ROI信息", ImVec2(-1, 0)))
        PrintROIToLog();
    if (ImGui::Button("清理图片", ImVec2(-1, 0)))
        ClearImage();

    ImGui::Spacing();

    ImGui::Separator();

    // 自定义日志输入
    static char inputBuf[256] = { 0 };
    ImGui::Text("自定义日志输入");
    ImGui::PushItemWidth(-1);
    ImGui::InputText("##log_input", inputBuf, sizeof(inputBuf));
    ImGui::PopItemWidth();
    if (ImGui::Button("发送到日志", ImVec2(-1, 28)))
    {
        if (strlen(inputBuf) > 0)
        {
            LogSystem::Add(LOG_INFO, color, "自定义: %s", inputBuf);
            inputBuf[0] = '\0';
        }
    }

    ImGui::End();
}

} // namespace UI
