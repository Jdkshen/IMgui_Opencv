#pragma once

#include "ToolInstance.h"

#include <cstddef>
#include <string>
#include <vector>

struct ToolChainPreflightIssue
{
    int toolIndex = -1;
    std::string message;
};

struct ToolChainPreflightResult
{
    std::vector<ToolChainPreflightIssue> issues;
    bool valid() const { return issues.empty(); }
};

namespace ToolChainPreflight
{
    ToolChainPreflightResult Check(const std::vector<ToolInstance>& tools,
        bool hasImage, std::size_t visibleRoiCount);
}
