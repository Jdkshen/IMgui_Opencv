#include "ToolChainValidator.h"

#include <functional>
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
                             const char* dependencyName)
    {
        if (configuredIndex < 0 && configuredId == 0)
            return;

        ToolChainDependency dependency;
        dependency.kind = kind;
        dependency.consumerIndex = consumerIndex;
        dependency.sourceToolId = configuredId;
        dependency.sourceIndex = ResolveSource(tools, configuredIndex, configuredId);
        if (dependency.sourceIndex < 0 ||
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
        else
        {
            dependency.valid = true;
        }
        dependencies.push_back(std::move(dependency));
    };

    for (int index = 0; index < static_cast<int>(tools.size()); ++index)
    {
        addDependency(index, tools[index].resultRoiSourceTool,
            tools[index].resultRoiSourceToolId, ToolDependencyKind::ResultROI, "结果 ROI");
        if (tools[index].fixture.enabled)
        {
            addDependency(index, tools[index].fixture.sourceToolIndex,
                tools[index].fixture.sourceToolId, ToolDependencyKind::Fixture, "Fixture");
        }
    }
    return dependencies;
}

ToolChainValidationResult Validate(const std::vector<ToolInstance>& tools)
{
    ToolChainValidationResult result;
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
