#include "Sidebar.h"
#include "DockSpaceHost.h"
#include "ImageViewer.h"
#include "ROIManager.h"
#include "../Core/ThemeManager.h"
#include "../Core/ROIState.h"
#include "../Core/ImageState.h"
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
        ImGui::SetItemTooltip(
            "右键创建：矩形/线/圆拖动，点单击，多边形逐点单击并用双击、Enter或首点闭合。\n"
            "左键选择和编辑，Delete/Backspace删除，Esc取消当前绘制。");

        SectionTitle("ROI 专业编辑");
        ImGui::BeginDisabled(!ROIState::CanUndo());
        if (ImGui::Button("撤销 Ctrl+Z", ImVec2((ImGui::GetContentRegionAvail().x - 5.0f) * 0.5f, 0)))
        {
            if (ROIState::Undo()) MarkCurrentRecipeDirty();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!ROIState::CanRedo());
        if (ImGui::Button("重做 Ctrl+Y", ImVec2(-1, 0)))
        {
            if (ROIState::Redo()) MarkCurrentRecipeDirty();
        }
        ImGui::EndDisabled();
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z) && ROIState::Undo())
            MarkCurrentRecipeDirty();
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y) && ROIState::Redo())
            MarkCurrentRecipeDirty();

        const int professionalIndex = ROIState::SelectedIndex();
        const ROI* professionalROI = ROIState::At(professionalIndex);
        if (professionalROI)
        {
            ROI edited = *professionalROI;
            bool flagsChanged = false;
            flagsChanged |= ImGui::Checkbox("锁定##roi_locked", &edited.locked);
            ImGui::SameLine();
            flagsChanged |= ImGui::Checkbox("显示##roi_visible", &edited.visible);
            ImGui::SameLine();
            flagsChanged |= ImGui::Checkbox("限制在图像内##roi_constrain", &edited.constrainToImage);
            if (flagsChanged)
            {
                edited.ClampToImage(ImageState::Current().size());
                ROIState::Update(professionalIndex, edited);
                MarkCurrentRecipeDirty();
            }

            bool numericChanged = false;
            bool numericActivated = false;
            bool numericCommitted = false;
            bool numericDeactivated = false;
            auto DragValue = [&](const char* label, float* value, float speed = 0.1f)
            {
                const bool changed = ImGui::DragFloat(label, value, speed, -1000000.0f,
                    1000000.0f, "%.3f");
                numericChanged |= changed;
                numericActivated |= ImGui::IsItemActivated();
                numericCommitted |= ImGui::IsItemDeactivatedAfterEdit();
                numericDeactivated |= ImGui::IsItemDeactivated();
            };

            ImGui::BeginDisabled(edited.locked);
            if (edited.type == ROI_TYPE_RECT)
            {
                float row = edited.HalconRow();
                float column = edited.HalconColumn();
                float phiDegrees = ROI::NormalizeRectangle2AngleDegrees(edited.angle);
                float length1 = edited.HalconLength1();
                float length2 = edited.HalconLength2();
                DragValue("Row##roi_row", &row);
                DragValue("Column##roi_column", &column);
                DragValue("Phi (deg)##roi_phi", &phiDegrees);
                DragValue("Length1##roi_length1", &length1);
                DragValue("Length2##roi_length2", &length2);
                if (numericChanged)
                {
                    length1 = (std::max)(0.5f, length1);
                    length2 = (std::max)(0.5f, length2);
                    edited.start = ImVec2(column - length1, row - length2);
                    edited.end = ImVec2(column + length1, row + length2);
                    edited.angle = ROI::NormalizeRectangle2AngleDegrees(phiDegrees);
                }
            }
            else if (edited.type == ROI_TYPE_CIRCLE)
            {
                float row = edited.start.y;
                float column = edited.start.x;
                float radius = edited.CircleRadius();
                DragValue("Row##circle_row", &row);
                DragValue("Column##circle_column", &column);
                DragValue("Radius##circle_radius", &radius);
                if (numericChanged)
                {
                    radius = (std::max)(0.5f, radius);
                    edited.start = ImVec2(column, row);
                    edited.end = ImVec2(column + radius, row);
                }
            }
            else if (edited.type == ROI_TYPE_POINT)
            {
                DragValue("Row##point_row", &edited.start.y);
                DragValue("Column##point_column", &edited.start.x);
            }
            else if (edited.type == ROI_TYPE_LINE)
            {
                DragValue("Row1##line_row1", &edited.start.y);
                DragValue("Column1##line_column1", &edited.start.x);
                DragValue("Row2##line_row2", &edited.end.y);
                DragValue("Column2##line_column2", &edited.end.x);
            }
            else if (edited.type == ROI_TYPE_POLYGON)
            {
                for (std::size_t pointIndex = 0; pointIndex < edited.points.size(); ++pointIndex)
                {
                    ImGui::PushID(static_cast<int>(pointIndex));
                    char label[32];
                    snprintf(label, sizeof(label), "P%zu (Column,Row)", pointIndex + 1);
                    numericChanged |= ImGui::DragFloat2(label, &edited.points[pointIndex].x,
                        0.1f, -1000000.0f, 1000000.0f, "%.3f");
                    numericActivated |= ImGui::IsItemActivated();
                    numericCommitted |= ImGui::IsItemDeactivatedAfterEdit();
                    numericDeactivated |= ImGui::IsItemDeactivated();
                    ImGui::PopID();
                }
            }
            ImGui::EndDisabled();
            if (numericActivated)
                ROIState::BeginHistoryTransaction();
            if (numericChanged)
            {
                edited.ClampToImage(ImageState::Current().size());
                ROIState::Update(professionalIndex, edited);
                MarkCurrentRecipeDirty();
            }
            if (numericCommitted)
                ROIState::CommitHistoryTransaction();
            else if (numericDeactivated)
                ROIState::CancelHistoryTransaction();

            ImGui::BeginDisabled(edited.locked);
            if (ImGui::Button("复制 ROI", ImVec2((ImGui::GetContentRegionAvail().x - 5.0f) * 0.5f, 0)))
            {
                ROI duplicate = edited;
                duplicate.runtimeId = 0;
                duplicate.locked = false;
                duplicate.start.x += 10.0f; duplicate.start.y += 10.0f;
                duplicate.end.x += 10.0f; duplicate.end.y += 10.0f;
                for (ImVec2& point : duplicate.points) { point.x += 10.0f; point.y += 10.0f; }
                duplicate.ClampToImage(ImageState::Current().size());
                EnsureROIRuntimeId(duplicate);
                ROIState::Add(std::move(duplicate), true);
                MarkCurrentRecipeDirty();
            }
            ImGui::SameLine();
            if (ImGui::Button("删除 ROI", ImVec2(-1, 0)))
            {
                if (ROIState::RemoveAt(professionalIndex)) MarkCurrentRecipeDirty();
            }
            ImGui::EndDisabled();
        }

        const int selectedROIIndex = ROIState::SelectedIndex();
        const ROI* selectedROI = ROIState::At(selectedROIIndex);
        if (selectedROI && selectedROI->type == ROI_TYPE_RECT && !selectedROI->locked)
        {
            ROI editedROI = *selectedROI;
            ImGui::TextDisabled("矩形角度");
            ImGui::SetNextItemWidth(-52.0f);
            if (ImGui::DragFloat("##roi_angle", &editedROI.angle, 0.1f,
                                 -90.0f, 90.0f, "%.2f deg"))
            {
                editedROI.angle = ROI::NormalizeRectangle2AngleDegrees(editedROI.angle);
                ROIState::Update(selectedROIIndex, editedROI);
                MarkCurrentRecipeDirty();
            }
            ImGui::SameLine();
            if (ImGui::Button("归零##roi_angle_reset"))
            {
                editedROI.angle = 0.0f;
                ROIState::Update(selectedROIIndex, editedROI);
                MarkCurrentRecipeDirty();
            }
        }

        if (ImGui::Button("清除当前类型 ROI", ImVec2(-1, 0)))
        {
            ROIState::RemoveByType(gCurrentROIType);
            gActiveHandle = HANDLE_NONE;
            MarkCurrentRecipeDirty();
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
