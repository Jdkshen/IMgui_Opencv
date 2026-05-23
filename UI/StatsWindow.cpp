#include "../Windows_imgui.h"
#include "StatsWindow.h"

namespace UI
{

void ShowStatsWindow()
{
    if (!g_ShowStats) return;

    ImGui::Begin("性能统计", &g_ShowStats);

    ImGuiIO& io = ImGui::GetIO();

    ImGui::Text("FPS: %.1f", io.Framerate);
    ImGui::Text("帧耗时: %.3f ms", 1000.0f / io.Framerate);

    ImGui::Separator();

    ImGui::Text("渲染: DX12 (Direct3D 12)");
    ImGui::Text("Draw Calls: (可扩展)");
    ImGui::Text("三角形数: (可接渲染统计)");

    ImGui::End();
}

} // namespace UI
