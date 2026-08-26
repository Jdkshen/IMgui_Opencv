#pragma once

#include "ToolChainState.h"
#include "ToolInstance.h"

#include <string>
#include <vector>

namespace ToolRunPolicy
{
    bool MatchesScope(const ToolInstance& tool, bool taskGroupRun,
        const std::string& taskGroupName);
    int TaskGroupCameraIndex(const std::vector<TaskGroupDefinition>& groups,
        const std::string& groupName);
    int EnabledTaskGroupCount(const std::vector<TaskGroupDefinition>& groups);
    std::vector<int> BuildExecutionOrder(const std::vector<ToolInstance>& tools,
        const std::vector<TaskGroupDefinition>& groups, bool taskGroupRun,
        const std::string& taskGroupName);
    bool RunScopeHasTaskImages(const std::vector<TaskGroupDefinition>& groups,
        bool taskGroupRun, const std::string& taskGroupName);
    bool IsAsyncTool(int toolType);
    bool HasCrossTaskDependencies(const std::vector<ToolInstance>& tools,
        const std::vector<int>& executionOrder);
}
