#include "../Windows_imgui.h"
#include "LogWindow.h"
#include "../Log/LogSystem.h"

namespace UI
{

void ShowLogWindow()
{
    if (!g_ShowLog) return;

    ImGui::Begin("日志窗口", &g_ShowLog);

    if (ImGui::Button("清空日志"))
        LogSystem::Clear();

    ImGui::Separator();

    ImGui::BeginChild("滚动区域");

    // ⭐ 优化：shared_ptr COW，零拷贝获取日志列表
    auto logs = LogSystem::GetLogs();
    if (!logs) { ImGui::EndChild(); ImGui::End(); return; }

    // ⭐ 优化：ImGuiListClipper 虚拟列表，只渲染可见行（~30条而非2000条）
    ImGuiListClipper clipper;
    clipper.Begin((int)logs->size());

    while (clipper.Step())
    {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
        {
            const auto& log = (*logs)[i];

            ImVec4 color;
            if (log.useCustomColor) { color = log.color; }
            else
            {
                switch (log.level)
                {
                case LOG_INFO:  color = ImVec4(0.8f, 0.8f, 0.8f, 1); break;
                case LOG_WARN:  color = ImVec4(1.0f, 0.8f, 0.2f, 1); break;
                case LOG_ERROR: color = ImVec4(1.0f, 0.3f, 0.3f, 1); break;
                }
            }

            ImGui::PushStyleColor(ImGuiCol_Text, color);

            // ⭐ 优化：使用预格式化的 displayText，无需每帧 snprintf
            if (ImGui::Selectable(log.displayText.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {}

            if (ImGui::BeginPopupContextItem())
            {
                if (ImGui::MenuItem("复制"))
                    ImGui::SetClipboardText(log.displayText.c_str());
                ImGui::EndPopup();
            }

            if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_C) && ImGui::GetIO().KeyCtrl)
                ImGui::SetClipboardText(log.displayText.c_str());

            ImGui::PopStyleColor();
        }
    }

    // 自动滚到底部（新日志到达时）
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);

    ImGui::EndChild();
    ImGui::End();
}

} // namespace UI
