#pragma once

#include "ROI.h"
#include "ToolResultCapabilities.h"
#include "../Algorithm/ToolResult.h"

#include <string>
#include <vector>

enum class ResultROIMode : int
{
    Disabled = 0,
    NthResult = 1,
    AllResults = 2,
    SelectedPair = 3,
};

enum class MissingResultPolicy : int
{
    Skip = 0,
    Fail = 1,
};

enum class ResultROIOutputGeometry : int
{
    Bounds = 0,
    CenterPointsOrPreserveLines = 1,
    CenterPoint = 2,
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
    ToolSpatialResultChannel channel = ToolSpatialResultChannel::Auto;
    ResultROIOutputGeometry outputGeometry = ResultROIOutputGeometry::Bounds;
    bool requireLineResults = false;
};

struct ResultROIResolution
{
    bool available = false;
    std::vector<ROI> rois;
    std::string reason;
};

struct ResultROIChoice
{
    int resultIndex = 0;
    std::string label;
    ToolSpatialResultChannel channel = ToolSpatialResultChannel::Auto;
    int channelIndex = -1;
};

namespace ResultROIResolver
{
    ResultROIResolution Resolve(const ToolResult& source, const ResultROIRequest& request, cv::Size imageSize = {});
    std::vector<ResultROIChoice> ListChoices(const ToolResult& source,
        const ResultROIRequest& request);
}
