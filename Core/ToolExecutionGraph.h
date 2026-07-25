#pragma once

#include "ToolInstance.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct ToolExecutionGraphNode
{
    int toolIndex = -1;
    std::uint64_t toolId = 0;
    std::vector<int> dependencies;
    bool parallelizable = false;
};

struct ToolExecutionGraphPlan
{
    std::vector<ToolExecutionGraphNode> nodes;
    std::vector<std::vector<int>> levels;
    bool valid = true;
    std::string error;
};

struct ToolExecutionCacheKey
{
    std::uint64_t toolId = 0;
    std::uint64_t parameterRevision = 0;
    std::uint64_t runRevision = 0;
    int imageVersion = -1;
    std::uint64_t upstreamRevision = 0;

    bool operator==(const ToolExecutionCacheKey&) const = default;
};

namespace ToolExecutionGraph
{
    ToolExecutionGraphPlan Build(const std::vector<ToolInstance>& tools);
    std::uint64_t ComputeUpstreamRevision(
        const ToolExecutionGraphPlan& plan,
        const std::vector<ToolInstance>& tools,
        int toolIndex);
    bool TryGetCachedResult(const ToolExecutionCacheKey& key, ToolResult& result);
    void StoreCachedResult(const ToolExecutionCacheKey& key, const ToolResult& result);
    void ClearCache();
}
