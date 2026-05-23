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

    const auto& logs = LogSystem::GetLogs();

    for (auto& log : logs)
    {
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

        char buf[1024];
        snprintf(buf, sizeof(buf), "[%s] %s", log.time.c_str(), log.text.c_str());

        ImGui::PushStyleColor(ImGuiCol_Text, color);

        if (ImGui::Selectable(buf, false, ImGuiSelectableFlags_AllowDoubleClick)) {}

        if (ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("复制"))
                ImGui::SetClipboardText(buf);
            ImGui::EndPopup();
        }

        if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_C) && ImGui::GetIO().KeyCtrl)
            ImGui::SetClipboardText(buf);

        ImGui::PopStyleColor();
    }

    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);

    ImGui::EndChild();
    ImGui::End();
}

} // namespace UI
