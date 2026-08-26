#include "ToolRunPolicy.h"

#include "ResultROIResolver.h"

#include <algorithm>
#include <unordered_set>

namespace ToolRunPolicy
{
bool MatchesScope(const ToolInstance& tool, bool taskGroupRun,
    const std::string& taskGroupName)
{
    return !taskGroupRun || tool.groupName == taskGroupName;
}

int TaskGroupCameraIndex(const std::vector<TaskGroupDefinition>& groups,
    const std::string& groupName)
{
    if (groupName.empty())
        return -1;
    const auto found = std::find_if(groups.begin(), groups.end(),
        [&groupName](const TaskGroupDefinition& group)
        {
            return group.name == groupName;
        });
    if (found == groups.end() || !found->enabled)
        return -1;
    return found->cameraIndex >= 0
        ? found->cameraIndex : (found->cameraPreferred ? 0 : -1);
}

int EnabledTaskGroupCount(const std::vector<TaskGroupDefinition>& groups)
{
    return static_cast<int>(std::count_if(groups.begin(), groups.end(),
        [](const TaskGroupDefinition& group) { return group.enabled; }));
}

std::vector<int> BuildExecutionOrder(const std::vector<ToolInstance>& tools,
    const std::vector<TaskGroupDefinition>& groups, bool taskGroupRun,
    const std::string& taskGroupName)
{
    std::vector<int> order;
    order.reserve(tools.size());
    if (taskGroupRun)
    {
        for (int index = 0; index < static_cast<int>(tools.size()); ++index)
        {
            if (MatchesScope(tools[index], true, taskGroupName))
                order.push_back(index);
        }
        return order;
    }

    for (const TaskGroupDefinition& group : groups)
    {
        if (!group.enabled)
            continue;
        for (int index = 0; index < static_cast<int>(tools.size()); ++index)
        {
            if (tools[index].groupName == group.name)
                order.push_back(index);
        }
    }
    for (int index = 0; index < static_cast<int>(tools.size()); ++index)
    {
        if (tools[index].groupName.empty())
            order.push_back(index);
    }
    return order;
}

bool RunScopeHasTaskImages(const std::vector<TaskGroupDefinition>& groups,
    bool taskGroupRun, const std::string& taskGroupName)
{
    return std::any_of(groups.begin(), groups.end(),
        [taskGroupRun, &taskGroupName](const TaskGroupDefinition& group)
        {
            return group.enabled &&
                (!group.imagePath.empty() || !group.imageFolderPath.empty()) &&
                (!taskGroupRun || group.name == taskGroupName);
        });
}

bool IsAsyncTool(int toolType)
{
    switch (toolType)
    {
    case 1:
    case 4:
    case 6:
    case 11:
    case 13:
    case 15:
        return true;
    default:
        return false;
    }
}

bool HasCrossTaskDependencies(const std::vector<ToolInstance>& tools,
    const std::vector<int>& executionOrder)
{
    const std::unordered_set<int> scheduled(
        executionOrder.begin(), executionOrder.end());
    const auto sourceIsCrossTask = [&tools](int legacyIndex,
        std::uint64_t stableId, const std::string& targetGroup)
    {
        int sourceIndex = -1;
        if (stableId != 0)
        {
            const auto found = std::find_if(tools.begin(), tools.end(),
                [stableId](const ToolInstance& tool)
                {
                    return tool.toolId == stableId;
                });
            if (found != tools.end())
                sourceIndex = static_cast<int>(std::distance(tools.begin(), found));
        }
        else if (legacyIndex >= 0 && legacyIndex < static_cast<int>(tools.size()))
        {
            sourceIndex = legacyIndex;
        }
        return sourceIndex >= 0 && tools[sourceIndex].groupName != targetGroup;
    };

    for (int toolIndex = 0; toolIndex < static_cast<int>(tools.size()); ++toolIndex)
    {
        const ToolInstance& tool = tools[toolIndex];
        if (!tool.enabled || !scheduled.contains(toolIndex))
            continue;
        if (tool.resultRoiMode != 0 && sourceIsCrossTask(
            tool.resultRoiSourceTool, tool.resultRoiSourceToolId, tool.groupName))
        {
            return true;
        }
        if (tool.resultRoiMode == static_cast<int>(ResultROIMode::SelectedPair) &&
            sourceIsCrossTask(tool.resultRoiSecondSourceTool,
                tool.resultRoiSecondSourceToolId, tool.groupName))
        {
            return true;
        }
        if (tool.fixture.enabled && sourceIsCrossTask(
            tool.fixture.sourceToolIndex, tool.fixture.sourceToolId, tool.groupName))
        {
            return true;
        }
    }
    return false;
}
}
