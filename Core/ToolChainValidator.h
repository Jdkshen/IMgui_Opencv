#pragma once

#include "ToolInstance.h"

#include <string>
#include <vector>

struct ToolChainValidationIssue
{
    int toolIndex = -1;
    std::string message;
};

struct ToolChainValidationResult
{
    std::vector<ToolChainValidationIssue> issues;
    bool valid() const { return issues.empty(); }
};

enum class ToolDependencyKind
{
    ResultROI,
    Fixture
};

struct ToolChainDependency
{
    ToolDependencyKind kind = ToolDependencyKind::ResultROI;
    int consumerIndex = -1;
    int sourceIndex = -1;
    std::uint64_t sourceToolId = 0;
    bool valid = false;
    std::string issue;
};

namespace ToolChainValidator
{
    std::vector<ToolChainDependency> DescribeDependencies(
        const std::vector<ToolInstance>& tools);
    ToolChainValidationResult Validate(const std::vector<ToolInstance>& tools);
}
