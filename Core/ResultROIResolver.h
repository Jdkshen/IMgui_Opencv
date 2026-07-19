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
    std::string category;
    int classId = -1;
    float minScore = -1.0f;
    float minArea = -1.0f;
    int sortMode = 0;
    bool sortDescending = true;
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
