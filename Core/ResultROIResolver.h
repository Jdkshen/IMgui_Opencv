#pragma once

#include "ROI.h"
#include "../Algorithm/ToolResult.h"

#include <string>
#include <vector>

enum class ResultROIMode : int
{
    Disabled = 0,
    NthResult = 1,
    AllResults = 2,
};

enum class MissingResultPolicy : int
{
    Skip = 0,
    Fail = 1,
};

struct ResultROIRequest
{
    ResultROIMode mode = ResultROIMode::Disabled;
    int resultIndex = 0;
    MissingResultPolicy missingPolicy = MissingResultPolicy::Skip;
};

struct ResultROIResolution
{
    bool available = false;
    std::vector<ROI> rois;
    std::string reason;
};

namespace ResultROIResolver
{
    ResultROIResolution Resolve(const ToolResult& source, const ResultROIRequest& request, cv::Size imageSize = {});
}

