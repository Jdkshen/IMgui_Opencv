#include "../Windows_imgui.h"
#include "LogWindow.h"
#include "../Log/LogSystem.h"

namespace UI
{
    static ImVec4 MakeLogColorReadable(const ImVec4& in, bool isDark)
    {
        if (isDark)
            return in;

        ImVec4 c = in;
        c.w = 1.0f;

        float maxCh = (c.x > c.y ? c.x : c.y);
        maxCh = (maxCh > c.z ? maxCh : c.z);
        float minCh = (c.x < c.y ? c.x : c.y);
        minCh = (minCh < c.z ? minCh : c.z);
        float brightness = (c.x * 0.299f) + (c.y * 0.587f) + (c.z * 0.114f);

        if (brightness > 0.55f || maxCh > 0.85f)
        {
            c.x = 0.10f + c.x * 0.38f;
            c.y = 0.12f + c.y * 0.36f;
            c.z = 0.14f + c.z * 0.34f;
        }

        if ((maxCh - minCh) < 0.12f && brightness > 0.45f)
            c = ImVec4(0.20f, 0.28f, 0.38f, 1.0f);

        return c;
    }


    void ShowLogWindow()
    {
        if (!g_ShowLog)
            return;

        ImGui::Begin("日志窗口", &g_ShowLog);
        const bool isDark = (g_CurrentTheme == 0);

        if (ImGui::Button("清空日志"))
            LogSystem::Clear();
        ImGui::SameLine();
        if (ImGui::Button("复制全部"))
        {
            std::string all;
            auto logs = LogSystem::GetLogs();
            if (logs)
            {
                for (const auto& l : *logs)
                    all += l.displayText + "\n";
                ImGui::SetClipboardText(all.c_str());
            }
        }

        ImGui::Separator();

        ImGui::BeginChild("滚动区域");

        // ⭐ 优化：shared_ptr COW，零拷贝获取日志列表
        auto logs = LogSystem::GetLogs();
        if (!logs)
        {
            ImGui::EndChild();
            ImGui::End();
            return;
        }

        // ⭐ 优化：ImGuiListClipper 虚拟列表，只渲染可见行（~30条而非2000条）
        ImGuiListClipper clipper;
        clipper.Begin((int)logs->size());

        while (clipper.Step())
        {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
            {
                const auto &log = (*logs)[i];
                ImGui::PushID(i);

                ImVec4 color;
                if (log.useCustomColor)
                {
                    color = MakeLogColorReadable(log.color, isDark);
                }
                else
                {
                    switch (log.level)
                    {
                    case LOG_INFO:
                        color = isDark ? ImVec4(0.8f, 0.8f, 0.8f, 1) : ImVec4(0.20f, 0.30f, 0.42f, 1);
                        break;
                    case LOG_WARN:
                        color = isDark ? ImVec4(1.0f, 0.8f, 0.2f, 1) : ImVec4(0.78f, 0.42f, 0.06f, 1);
                        break;
                    case LOG_ERROR:
                        color = isDark ? ImVec4(1.0f, 0.3f, 0.3f, 1) : ImVec4(0.74f, 0.16f, 0.16f, 1);
                        break;
                    }
                }

                ImGui::PushStyleColor(ImGuiCol_Text, color);

                // ⭐ 优化：使用预格式化的 displayText，无需每帧 snprintf
                if (ImGui::Selectable(log.displayText.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
                {
                }

                if (ImGui::BeginPopupContextItem())
                {
                    if (ImGui::MenuItem("复制此行"))
                        ImGui::SetClipboardText(log.displayText.c_str());
                    if (ImGui::MenuItem("复制全部"))
                    {
                        std::string all;
                        for (const auto& l : *logs)
                            all += l.displayText + "\n";
                        ImGui::SetClipboardText(all.c_str());
                    }
                    ImGui::EndPopup();
                }

                if (ImGui::IsItemHovered())
                {
                    if (ImGui::IsKeyPressed(ImGuiKey_C) && ImGui::GetIO().KeyCtrl)
                        ImGui::SetClipboardText(log.displayText.c_str());
                    if (ImGui::IsKeyPressed(ImGuiKey_A) && ImGui::GetIO().KeyCtrl)
                    {
                        std::string all;
                        for (const auto& l : *logs)
                            all += l.displayText + "\n";
                        ImGui::SetClipboardText(all.c_str());
                    }
                }

                ImGui::PopStyleColor();
                ImGui::PopID();
            }
        }

        // 自动滚到底部（新日志到达时）
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);

        ImGui::EndChild();
        ImGui::End();
    }

} // namespace UI
