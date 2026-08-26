#include "ResultOverlayState.h"

#include "FixtureTransform.h"
#include "RealtimeDetectionState.h"
#include "TemplateState.h"
#include "ToolChainState.h"
#include "ToolResultUtils.h"
#include "VisionContext.h"

#include <algorithm>

namespace
{
ResultOverlayState::Settings s_settings;
bool s_taskGroupFilterEnabled = false;
std::string s_taskGroupFilter;
std::vector<ToolResult> s_filteredResults;

const ToolInstance* SourceTool(const ToolResult& result)
{
    const auto& tools = ToolChainState::ReadOnlyTools();
    if (result.sourceToolId != 0)
    {
        const auto found = std::find_if(tools.begin(), tools.end(),
            [&result](const ToolInstance& tool)
            {
                return tool.toolId == result.sourceToolId;
            });
        if (found != tools.end())
            return &*found;
    }
    if (result.sourceToolIndex >= 0 &&
        result.sourceToolIndex < static_cast<int>(tools.size()))
    {
        return &tools[static_cast<std::size_t>(result.sourceToolIndex)];
    }
    return nullptr;
}

bool MatchesTaskGroupFilter(const ToolInstance& tool)
{
    return !s_taskGroupFilterEnabled || tool.groupName == s_taskGroupFilter;
}
}

namespace ResultOverlayState
{
Settings& MutableSettings()
{
    return s_settings;
}

const Settings& ReadOnlySettings()
{
    return s_settings;
}

const std::vector<ToolResult>& Results()
{
    if (!s_taskGroupFilterEnabled)
        return gContext.unifiedResults;

    s_filteredResults.clear();
    for (const ToolResult& result : gContext.unifiedResults)
    {
        const ToolInstance* source = SourceTool(result);
        if (source && MatchesTaskGroupFilter(*source))
            s_filteredResults.push_back(result);
    }
    return s_filteredResults;
}

void SetTaskGroupFilter(const std::string& groupName)
{
    s_taskGroupFilterEnabled = true;
    s_taskGroupFilter = groupName;
    s_filteredResults.clear();
}

void ClearTaskGroupFilter()
{
    s_taskGroupFilterEnabled = false;
    s_taskGroupFilter.clear();
    s_filteredResults.clear();
}

bool HasTaskGroupFilter()
{
    return s_taskGroupFilterEnabled;
}

const std::vector<DetectedObject>& RealtimeObjects()
{
    return RealtimeDetectionState::Objects();
}

bool IsRealtimeOverlayVisible()
{
    return RealtimeDetectionState::IsOverlayVisible();
}

float RealtimeOverlayOffsetX()
{
    return RealtimeDetectionState::OverlayOffsetX();
}

std::vector<FixtureOverlay> FixtureOverlays()
{
    const auto& tools = ToolChainState::ReadOnlyTools();
    std::vector<FixtureOverlay> overlays;
    overlays.reserve(tools.size());

    for (const ToolInstance& tool : tools)
    {
        if (!MatchesTaskGroupFilter(tool) ||
            !tool.fixture.enabled || !tool.hasLastResult)
            continue;

        int sourceIndex = tool.fixture.sourceToolIndex;
        if (tool.fixture.sourceToolId != 0)
        {
            sourceIndex = -1;
            for (int index = 0; index < static_cast<int>(tools.size()); ++index)
            {
                if (tools[index].toolId == tool.fixture.sourceToolId)
                {
                    sourceIndex = index;
                    break;
                }
            }
        }
        if (sourceIndex < 0 || sourceIndex >= static_cast<int>(tools.size()) ||
            !tools[sourceIndex].hasLastResult)
            continue;

        FixturePose currentPose;
        if (!FixtureTransform::TryExtractPose(
                tools[sourceIndex].lastResult,
                tool.fixture.resultIndex, currentPose,
                static_cast<ToolSpatialResultChannel>(tool.fixture.resultChannel)))
            continue;

        FixtureOverlay overlay;
        overlay.referenceOrigin = tool.fixture.referenceOrigin;
        overlay.referenceAngleDegrees = tool.fixture.referenceAngleDegrees;
        overlay.currentOrigin = currentPose.origin;
        overlay.currentAngleDegrees = currentPose.angleDegrees;
        overlay.showLabel = tool.showResultLabels;
        overlays.push_back(overlay);
    }
    return overlays;
}

void ClearResults()
{
    TemplateState::ClearResults();
    RealtimeDetectionState::Clear();
    gContext.ClearUnifiedResults();
    s_filteredResults.clear();
}

bool ShouldDrawResultLabels(const ToolResult& result)
{
    if (!s_settings.showLabels)
        return false;

    const auto& tools = ToolChainState::ReadOnlyTools();
    if (result.sourceToolId != 0)
    {
        for (const ToolInstance& tool : tools)
        {
            if (tool.toolId == result.sourceToolId)
                return tool.showResultLabels;
        }
    }
    if (result.sourceToolIndex < 0 || result.sourceToolIndex >= static_cast<int>(tools.size()))
        return true;
    return tools[result.sourceToolIndex].showResultLabels;
}

int MaxVisibleLabels()
{
    return (std::max)(0, s_settings.maxVisibleLabels);
}

bool ShouldDrawRegionLabel(const ToolResult& result, const std::string& label)
{
    return ShouldDrawResultLabels(result) && !label.empty() &&
        !ToolResultHasDuplicateTextLabel(result, label);
}

std::string BuildLabel(const ToolResult& result, const std::string& itemLabel)
{
    return BuildToolResultOverlayLabel(result, itemLabel);
}
}
