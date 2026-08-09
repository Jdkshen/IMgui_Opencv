#include "ToolChainValidator.h"
#include "ResultROIResolver.h"
#include "ToolResultCapabilities.h"

#include <functional>
#include <unordered_map>
#include <utility>

namespace
{
int ResolveSource(const std::vector<ToolInstance>& tools, int index, std::uint64_t id)
{
    if (id != 0)
    {
        for (int i = 0; i < static_cast<int>(tools.size()); ++i)
            if (tools[i].toolId == id)
                return i;
        return -1;
    }
    return index;
}
}

namespace ToolChainValidator
{
std::vector<ToolChainDependency> DescribeDependencies(const std::vector<ToolInstance>& tools)
{
    std::vector<ToolChainDependency> dependencies;
    auto addDependency = [&](int consumerIndex, int configuredIndex,
                             std::uint64_t configuredId, ToolDependencyKind kind,
                             const char* dependencyName, bool requireLineResult,
                             int configuredChannel)
    {
        ToolChainDependency dependency;
        dependency.kind = kind;
        dependency.consumerIndex = consumerIndex;
        dependency.sourceToolId = configuredId;
        dependency.sourceIndex = ResolveSource(tools, configuredIndex, configuredId);
        if (configuredIndex < 0 && configuredId == 0)
        {
            dependency.issue = std::string(dependencyName) + "未选择上游工具";
        }
        else if (dependency.sourceIndex < 0 ||
            dependency.sourceIndex >= static_cast<int>(tools.size()))
        {
            dependency.issue = std::string(dependencyName) + "上游工具无效";
        }
        else if (dependency.sourceIndex == consumerIndex)
        {
            dependency.issue = std::string(dependencyName) + "不能依赖自身";
        }
        else if (dependency.sourceIndex >= consumerIndex)
        {
            dependency.issue = std::string(dependencyName) + "必须指向前面的工具";
        }
        else if (!tools[dependency.sourceIndex].enabled)
        {
            dependency.issue = std::string(dependencyName) + "上游工具已禁用";
        }
        else if (!IsValidSpatialResultChannel(configuredChannel))
        {
            dependency.issue = std::string(dependencyName) + "输出通道无效";
        }
        else if (kind == ToolDependencyKind::ResultROI && requireLineResult &&
            !ToolCapabilitiesForType(tools[dependency.sourceIndex].type).lines)
        {
            dependency.issue = std::string(dependencyName) + "上游工具不产生线段结果";
        }
        else if (kind == ToolDependencyKind::ResultROI &&
            !ToolCapabilitiesForType(tools[dependency.sourceIndex].type).SupportsChannel(
                requireLineResult ? ToolSpatialResultChannel::Lines :
                    static_cast<ToolSpatialResultChannel>(configuredChannel)))
        {
            dependency.issue = std::string(dependencyName) + "上游工具不产生所选空间结果通道";
        }
        else if (kind == ToolDependencyKind::Fixture &&
            !ToolCapabilitiesForType(tools[dependency.sourceIndex].type).SupportsChannel(
                static_cast<ToolSpatialResultChannel>(configuredChannel)))
        {
            dependency.issue = "Fixture 上游工具不产生可定位结果";
        }
        else
        {
            dependency.valid = true;
        }
        dependencies.push_back(std::move(dependency));
    };

    for (int index = 0; index < static_cast<int>(tools.size()); ++index)
    {
        if (!tools[index].enabled)
            continue;
        if (tools[index].resultRoiMode != 0)
        {
            const bool selectedPair = tools[index].resultRoiMode == 3;
            const bool firstRequiresLine = selectedPair && tools[index].type == 15 &&
                (tools[index].measureMode == 2 || tools[index].measureMode == 7);
            addDependency(index, tools[index].resultRoiSourceTool,
                tools[index].resultRoiSourceToolId,
                ToolDependencyKind::ResultROI,
                selectedPair ? "结果 ROI A" : "结果 ROI", firstRequiresLine,
                tools[index].resultRoiChannel);
            if (selectedPair)
            {
                const bool secondRequiresLine = tools[index].type == 15 &&
                    (tools[index].measureMode == 2 || tools[index].measureMode == 6 ||
                     tools[index].measureMode == 7);
                addDependency(index, tools[index].resultRoiSecondSourceTool,
                    tools[index].resultRoiSecondSourceToolId,
                    ToolDependencyKind::ResultROI, "结果 ROI B", secondRequiresLine,
                    tools[index].resultRoiSecondChannel);
            }
        }
        if (tools[index].fixture.enabled)
        {
            addDependency(index, tools[index].fixture.sourceToolIndex,
                tools[index].fixture.sourceToolId, ToolDependencyKind::Fixture,
                "Fixture", false, tools[index].fixture.resultChannel);
        }
    }
    return dependencies;
}

ToolChainValidationResult Validate(const std::vector<ToolInstance>& tools)
{
    ToolChainValidationResult result;
    std::unordered_map<std::uint64_t, int> toolIds;
    for (int index = 0; index < static_cast<int>(tools.size()); ++index)
    {
        const ToolInstance& tool = tools[index];
        if (tool.toolId != 0)
        {
            if (!toolIds.emplace(tool.toolId, index).second)
                result.issues.push_back({index, "工具 ID 重复，无法稳定解析上下游引用"});
        }
        if (!tool.enabled)
            continue;
        if (tool.resultRoiMode < static_cast<int>(ResultROIMode::Disabled) ||
            tool.resultRoiMode > static_cast<int>(ResultROIMode::SelectedPair))
        {
            result.issues.push_back({index, "结果 ROI 模式无效"});
        }
        if (tool.resultRoiMode != static_cast<int>(ResultROIMode::Disabled))
        {
            if (tool.resultRoiIndex < 0)
                result.issues.push_back({index, "结果 ROI A 的结果序号不能小于 1"});
            if (tool.resultRoiMissingPolicy < 0 || tool.resultRoiMissingPolicy > 1)
                result.issues.push_back({index, "结果 ROI 缺失处理策略无效"});
        }
        if (tool.resultRoiMode == static_cast<int>(ResultROIMode::SelectedPair) &&
            tool.resultRoiSecondIndex < 0)
        {
            result.issues.push_back({index, "结果 ROI B 的结果序号不能小于 1"});
        }
        if (tool.fixture.enabled && tool.fixture.resultIndex < 0)
            result.issues.push_back({index, "Fixture 结果序号不能小于 1"});
    }
    std::vector<std::vector<int>> edges(tools.size());
    for (const ToolChainDependency& dependency : DescribeDependencies(tools))
    {
        if (!dependency.valid)
            result.issues.push_back({dependency.consumerIndex, dependency.issue});
        if (dependency.sourceIndex >= 0 &&
            dependency.sourceIndex < static_cast<int>(tools.size()) &&
            dependency.sourceIndex != dependency.consumerIndex)
        {
            // Keep forward edges so DFS can still report cycles in imported recipes.
            edges[dependency.consumerIndex].push_back(dependency.sourceIndex);
        }
    }

    std::vector<int> state(tools.size(), 0);
    std::function<void(int)> visit = [&](int index)
    {
        if (state[index] == 2)
            return;
        if (state[index] == 1)
        {
            result.issues.push_back({index, "工具链存在循环依赖"});
            return;
        }
        state[index] = 1;
        for (int source : edges[index])
            visit(source);
        state[index] = 2;
    };
    for (int i = 0; i < static_cast<int>(tools.size()); ++i)
        visit(i);
    return result;
}
}
