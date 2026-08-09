#include "ToolExecutionGraph.h"

#include "ResultROIResolver.h"
#include "ToolChainValidator.h"

#include <algorithm>
#include <deque>
#include <mutex>
#include <unordered_set>

namespace
{
struct CacheKeyHash
{
    std::size_t operator()(const ToolExecutionCacheKey& key) const noexcept
    {
        std::size_t value = static_cast<std::size_t>(key.toolId);
        value ^= static_cast<std::size_t>(key.parameterRevision) + 0x9e3779b9u +
            (value << 6) + (value >> 2);
        value ^= static_cast<std::size_t>(key.runRevision) + 0x9e3779b9u +
            (value << 6) + (value >> 2);
        value ^= static_cast<std::size_t>(key.imageVersion) + 0x9e3779b9u +
            (value << 6) + (value >> 2);
        value ^= static_cast<std::size_t>(key.upstreamRevision) + 0x9e3779b9u +
            (value << 6) + (value >> 2);
        return value;
    }
};

std::mutex s_cacheMutex;
std::unordered_map<ToolExecutionCacheKey, ToolResult, CacheKeyHash> s_cache;
constexpr std::size_t kMaximumCacheEntries = 256;

bool IsParallelizableType(int type)
{
    switch (type)
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

int ResolveSource(const std::vector<ToolInstance>& tools,
    int legacyIndex, std::uint64_t toolId)
{
    if (toolId != 0)
    {
        for (int index = 0; index < static_cast<int>(tools.size()); ++index)
        {
            if (tools[index].toolId == toolId)
                return index;
        }
        return -1;
    }
    return legacyIndex >= 0 && legacyIndex < static_cast<int>(tools.size())
        ? legacyIndex : -1;
}

void AddDependency(ToolExecutionGraphNode& node, int dependency)
{
    if (dependency < 0 || dependency == node.toolIndex ||
        std::find(node.dependencies.begin(), node.dependencies.end(), dependency) !=
            node.dependencies.end())
    {
        return;
    }
    node.dependencies.push_back(dependency);
}
}

namespace ToolExecutionGraph
{
ToolExecutionGraphPlan Build(const std::vector<ToolInstance>& tools)
{
    ToolExecutionGraphPlan plan;
    const ToolChainValidationResult validation = ToolChainValidator::Validate(tools);
    if (!validation.valid())
    {
        plan.valid = false;
        plan.error = validation.issues.front().message;
        return plan;
    }
    plan.nodes.resize(tools.size());
    int originalToolIndex = -1;
    for (int index = 0; index < static_cast<int>(tools.size()); ++index)
    {
        const ToolInstance& tool = tools[index];
        ToolExecutionGraphNode& node = plan.nodes[index];
        node.toolIndex = index;
        node.toolId = tool.toolId;
        node.parallelizable = tool.enabled && IsParallelizableType(tool.type) &&
            tool.inputSourceMode == 2 && !tool.judgement.stopOnFailure;
        if (tool.type == 12 && originalToolIndex < 0)
            originalToolIndex = index;
        else if (tool.inputSourceMode == 2 && originalToolIndex >= 0)
            AddDependency(node, originalToolIndex);

        if (tool.fixture.enabled)
        {
            AddDependency(node, ResolveSource(tools,
                tool.fixture.sourceToolIndex, tool.fixture.sourceToolId));
        }
        if (tool.resultRoiMode != static_cast<int>(ResultROIMode::Disabled))
        {
            AddDependency(node, ResolveSource(tools,
                tool.resultRoiSourceTool, tool.resultRoiSourceToolId));
            if (tool.resultRoiMode == static_cast<int>(ResultROIMode::SelectedPair))
            {
                AddDependency(node, ResolveSource(tools,
                    tool.resultRoiSecondSourceTool,
                    tool.resultRoiSecondSourceToolId));
            }
        }
        if (tool.inputSourceMode == 1)
        {
            for (int previous = index - 1; previous >= 0; --previous)
            {
                if (tools[previous].enabled)
                {
                    AddDependency(node, previous);
                    break;
                }
            }
        }
        std::sort(node.dependencies.begin(), node.dependencies.end());
    }

    std::vector<int> indegree(tools.size(), 0);
    std::vector<std::vector<int>> dependents(tools.size());
    for (const ToolExecutionGraphNode& node : plan.nodes)
    {
        indegree[node.toolIndex] = static_cast<int>(node.dependencies.size());
        for (int dependency : node.dependencies)
            dependents[dependency].push_back(node.toolIndex);
    }

    std::vector<int> ready;
    for (int index = 0; index < static_cast<int>(indegree.size()); ++index)
    {
        if (indegree[index] == 0)
            ready.push_back(index);
    }
    std::size_t visited = 0;
    while (!ready.empty())
    {
        std::sort(ready.begin(), ready.end());
        plan.levels.push_back(ready);
        visited += ready.size();
        std::vector<int> next;
        for (int index : ready)
        {
            for (int dependent : dependents[index])
            {
                if (--indegree[dependent] == 0)
                    next.push_back(dependent);
            }
        }
        ready = std::move(next);
    }

    if (visited != tools.size())
    {
        plan.valid = false;
        plan.error = "工具链存在循环依赖";
        plan.levels.clear();
    }
    return plan;
}

std::uint64_t ComputeUpstreamRevision(
    const ToolExecutionGraphPlan& plan,
    const std::vector<ToolInstance>& tools,
    int toolIndex)
{
    if (toolIndex < 0 || toolIndex >= static_cast<int>(plan.nodes.size()))
        return 0;
    std::uint64_t revision = 1469598103934665603ull;
    for (int dependency : plan.nodes[toolIndex].dependencies)
    {
        if (dependency < 0 || dependency >= static_cast<int>(tools.size()))
            continue;
        const ToolInstance& upstream = tools[dependency];
        revision ^= upstream.toolId;
        revision *= 1099511628211ull;
        revision ^= upstream.parameterRevision;
        revision *= 1099511628211ull;
        revision ^= upstream.hasLastResult ? 1ull : 0ull;
        revision *= 1099511628211ull;
    }
    return revision;
}

bool TryGetCachedResult(const ToolExecutionCacheKey& key, ToolResult& result)
{
    std::lock_guard<std::mutex> lock(s_cacheMutex);
    const auto found = s_cache.find(key);
    if (found == s_cache.end())
        return false;
    result = found->second;
    return true;
}

void StoreCachedResult(const ToolExecutionCacheKey& key, const ToolResult& result)
{
    std::lock_guard<std::mutex> lock(s_cacheMutex);
    if (s_cache.size() >= kMaximumCacheEntries)
        s_cache.clear();
    s_cache[key] = result;
}

void ClearCache()
{
    std::lock_guard<std::mutex> lock(s_cacheMutex);
    s_cache.clear();
}
}
