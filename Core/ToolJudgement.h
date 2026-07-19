#pragma once

#include "../Algorithm/ToolResult.h"

#include <string>

struct ToolJudgementSettings
{
    bool enabled = false;
    bool stopOnFailure = false;
    int minResultCount = 1;
    int maxResultCount = -1;
    float minScore = -1.0f;
    float minArea = -1.0f;
    float maxArea = -1.0f;
    bool measurementRangeEnabled = false;
    std::string measurementName;
    double minMeasurement = 0.0;
    double maxMeasurement = 0.0;
    std::string requiredText;
    int textMatchMode = 0; // 0=contains, 1=equals
    bool textCaseSensitive = false;
};

namespace ToolJudgement
{
    int PrimaryResultCount(const ToolResult& result);
    void Evaluate(ToolResult& result, const ToolJudgementSettings& settings);
    bool ShouldStop(const ToolResult& result, const ToolJudgementSettings& settings);
}
