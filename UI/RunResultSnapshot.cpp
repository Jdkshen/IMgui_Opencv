#include "RunResultSnapshot.h"

#include "DockSpaceHost.h"
#include "RunResultPresentation.h"
#include "../Core/HardwareRuntimeService.h"
#include "../Core/ImageState.h"
#include "../Core/ToolChainState.h"
#include "../Core/ToolController.h"

#include <utility>

namespace UI::RunResultSnapshotModel
{
std::string SnapshotFailureReason(const RunResultSnapshot& snapshot)
{
    for (const RunResultRow& row : snapshot.rows)
    {
        if (row.status == ToolResultStatus::Fail ||
            row.status == ToolResultStatus::Error)
        {
            if (!row.summary.empty())
                return row.name + "：" + row.summary;
        }
    }
    return {};
}

RunResultSnapshot BuildSnapshot(const std::string* groupFilter)
{
    RunResultSnapshot next;
    next.valid = true;
    next.loopIteration = ToolController::GetLastCompletedLoopRound();
    next.loopRound = next.loopIteration > 0;
    next.captureSerial = ToolController::GetCompletedBatchSerial();
    next.recipeName = CurrentRecipeName();
    next.totalTimeMs = groupFilter ? 0.0f : ToolController::GetTotalTimeMs();
    if (groupFilter)
    {
        next.resultImage = ToolController::GetTaskResultImage(*groupFilter).clone();
        next.textureKey = 0x4000000000000000ULL;
        for (const TaskGroupDefinition& group : ToolChainState::ReadOnlyTaskGroups())
        {
            if (group.name == *groupFilter)
            {
                next.textureKey |= group.id;
                break;
            }
        }
    }
    else
    {
        next.resultImage = ImageState::Current().clone();
        next.textureKey = 0x8000000000000001ULL;
    }

    std::vector<ToolResult> aggregateResults;
    const auto& tools = ToolChainState::ReadOnlyTools();
    next.rows.reserve(tools.size());
    aggregateResults.reserve(tools.size());

    for (int index = 0; index < static_cast<int>(tools.size()); ++index)
    {
        const ToolInstance& tool = tools[index];
        if (!tool.enabled || (groupFilter && tool.groupName != *groupFilter))
            continue;

        RunResultRow row;
        row.index = static_cast<int>(next.rows.size()) + 1;
        row.name = RunResultPresentation::ToolDisplayName(tool);
        row.timeMs = ToolController::GetToolTimeMs(index);
        row.executed = tool.hasLastResult;
        if (groupFilter)
            next.totalTimeMs += row.timeMs;

        if (!tool.hasLastResult)
        {
            row.status = ToolResultStatus::Error;
            row.summary = "未执行";
            ++next.pendingCount;
        }
        else
        {
            row.status = tool.lastResult.status;
            row.skipped = tool.lastResult.skipped;
            row.summary = RunResultPresentation::ResultSummary(tool.lastResult);
            row.details = RunResultPresentation::ResultDetails(tool.lastResult);
            aggregateResults.push_back(tool.lastResult);
            ToolResult overlayResult = tool.lastResult;
            overlayResult.debugImage.release();
            next.overlayResults.push_back(std::move(overlayResult));

            if (row.skipped)
                ++next.skippedCount;
            else if (row.status == ToolResultStatus::Pass)
                ++next.passCount;
            else if (row.status == ToolResultStatus::Fail)
                ++next.failCount;
            else
                ++next.errorCount;
        }
        next.rows.push_back(std::move(row));
    }

    next.overallStatus = HardwareRuntimeService::AggregateInspectionStatus(aggregateResults);
    if (next.pendingCount > 0 && next.overallStatus == ToolResultStatus::Pass)
        next.overallStatus = ToolResultStatus::Error;
    return next;
}

std::vector<std::string> CollectTaskGroups()
{
    std::vector<std::string> groups;
    for (const TaskGroupDefinition& group : ToolChainState::ReadOnlyTaskGroups())
    {
        if (group.enabled)
            groups.push_back(group.name);
    }

    bool hasUngroupedTools = false;
    for (const ToolInstance& tool : ToolChainState::ReadOnlyTools())
    {
        if (tool.enabled && tool.groupName.empty())
            hasUngroupedTools = true;
    }
    if (hasUngroupedTools)
        groups.push_back({});
    return groups;
}
}
