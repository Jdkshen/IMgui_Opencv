#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ToolInstance;

namespace UI::TaskGroupWindow
{
    void DrawTaskGroupManagerWindows();
    void RefreshSelectedTaskPreviewAfterRun();
    void CommitTaskGroupChange();

    std::vector<int> CollectVisibleWorkflowToolIndices();
    std::string WorkflowChainTitle();
    bool BindSelectedTaskImagePath(const std::string& imagePath);

    bool ConsumeToolsWindowFocusRequest();
    void OpenManagerWindows();
    void SelectAllGroups();
    void SelectUngroupedTools();
    void SelectTaskGroupInTools(std::uint64_t groupId, const std::string& groupName);
    bool IsUngroupedFilter();
    bool IsAllGroupsFilter();
    const std::string& CurrentTaskGroupName();
    void AssignNewToolToCurrentGroup(ToolInstance& tool);
}
