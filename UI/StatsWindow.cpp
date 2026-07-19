#include "StatsWindow.h"
#include "DockSpaceHost.h"
#include "../include/imgui/imgui.h"
#include "../Core/InspectionHistory.h"
#include "../Core/OpenFileDialog.h"
#include "../Core/ThemeManager.h"
#include "../Log/LogSystem.h"

#include <algorithm>
#include <cfloat>
#include <string>
#include <vector>

namespace UI
{

void ShowStatsWindow()
{
    if (!g_ShowStats) return;

    ImGui::Begin("性能统计", &g_ShowStats);

    ImGuiIO& io = ImGui::GetIO();
    const bool isDark = g_CurrentTheme == 0;
    const ImVec4 metricColor = io.Framerate >= 50.0f
        ? (isDark ? ImVec4(0.34f, 0.78f, 0.48f, 1.0f) : ImVec4(0.05f, 0.40f, 0.19f, 1.0f))
        : (isDark ? ImVec4(0.95f, 0.68f, 0.26f, 1.0f) : ImVec4(0.70f, 0.36f, 0.05f, 1.0f));

    if (ImGui::BeginTable("##runtime_metrics", 2,
        ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV))
    {
        ImGui::TableNextColumn();
        ImGui::TextDisabled("帧率");
        ImGui::TextColored(metricColor, "%.1f FPS", io.Framerate);
        ImGui::TableNextColumn();
        ImGui::TextDisabled("帧耗时");
        ImGui::TextColored(metricColor, "%.3f ms", io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);
        ImGui::EndTable();
    }

    ImGui::SeparatorText("渲染后端");
    ImGui::Text("Direct3D 12");

    ImGui::SeparatorText("质量统计");
    if (ImGui::CollapsingHeader("SPC 检测历史", ImGuiTreeNodeFlags_DefaultOpen))
    {
        static std::string selectedMeasurement;
        static double nominal = 0.0;
        static double toleranceMinus = 0.0;
        static double tolerancePlus = 0.0;

        const std::vector<std::string> measurementNames = InspectionHistory::MeasurementNames();

        if (measurementNames.empty())
        {
            ImGui::TextDisabled("暂无测量历史");
        }
        else
        {
            if (std::find(measurementNames.begin(), measurementNames.end(), selectedMeasurement) == measurementNames.end())
                selectedMeasurement = measurementNames.front();

            if (ImGui::BeginCombo("测量项", selectedMeasurement.c_str()))
            {
                for (const std::string& name : measurementNames)
                {
                    const bool selected = name == selectedMeasurement;
                    if (ImGui::Selectable(name.c_str(), selected))
                        selectedMeasurement = name;
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputDouble("名义值", &nominal, 0.01, 0.1, "%.6f");
            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputDouble("下公差", &toleranceMinus, 0.01, 0.1, "%.6f");
            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputDouble("上公差", &tolerancePlus, 0.01, 0.1, "%.6f");

            const InspectionHistory::Statistics statistics = InspectionHistory::Compute(
                selectedMeasurement, nominal, toleranceMinus, tolerancePlus);
            ImGui::Text("样本: %zu | 均值: %.6f | 标准差: %.6f",
                statistics.count, statistics.mean, statistics.standardDeviation);
            if (statistics.count > 0)
                ImGui::Text("范围: %.6f ~ %.6f", statistics.minimum, statistics.maximum);
            if (statistics.hasTolerance)
                ImGui::Text("规格: %.6f ~ %.6f | Cp: %.3f | Cpk: %.3f",
                    statistics.lowerLimit, statistics.upperLimit, statistics.cp, statistics.cpk);

            constexpr std::size_t kMaxTrendPoints = 120;
            const std::vector<double> trendValues =
                InspectionHistory::Trend(selectedMeasurement, kMaxTrendPoints);
            std::vector<float> trend;
            trend.reserve(trendValues.size());
            for (double value : trendValues)
                trend.push_back(static_cast<float>(value));
            if (trend.size() >= 2)
            {
                ImGui::Text("最近样本趋势 (%zu)", trend.size());
                ImGui::PlotLines("##spc_trend", trend.data(), static_cast<int>(trend.size()),
                    0, nullptr, FLT_MAX, FLT_MAX, ImVec2(-1.0f, 110.0f));
            }

            if (ImGui::Button("导出 SPC CSV"))
            {
                const std::string path = SaveFileDialogWithFilter(
                    L"CSV 文件 (*.csv)\0*.csv\0所有文件 (*.*)\0*.*\0",
                    L"导出 SPC 历史", L"csv");
                if (!path.empty())
                {
                    if (InspectionHistory::ExportCsv(path.c_str()))
                        LogSystem::Add(LOG_INFO, "SPC 历史已导出：%s", path.c_str());
                    else
                        LogSystem::Add(LOG_ERROR, "SPC 历史导出失败：%s", path.c_str());
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("清空 SPC 历史"))
                InspectionHistory::Clear();
        }
    }

    ImGui::End();
}

} // namespace UI
