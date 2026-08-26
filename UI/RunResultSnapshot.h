#pragma once

#include "../Algorithm/ToolResult.h"

#include <opencv2/core/mat.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace UI::RunResultSnapshotModel
{
struct RunResultRow
{
    int index = 0;
    std::string name;
    ToolResultStatus status = ToolResultStatus::Error;
    bool skipped = false;
    bool executed = false;
    float timeMs = 0.0f;
    std::string summary;
    std::string details;
};

struct RunResultSnapshot
{
    bool valid = false;
    bool loopRound = false;
    std::uint64_t loopIteration = 0;
    std::string recipeName;
    ToolResultStatus overallStatus = ToolResultStatus::Error;
    float totalTimeMs = 0.0f;
    int passCount = 0;
    int failCount = 0;
    int errorCount = 0;
    int skippedCount = 0;
    int pendingCount = 0;
    std::vector<RunResultRow> rows;
    std::vector<ToolResult> overlayResults;
    cv::Mat resultImage;
    std::uint64_t textureKey = 0;
    std::uint64_t captureSerial = 0;
};

std::string SnapshotFailureReason(const RunResultSnapshot& snapshot);
RunResultSnapshot BuildSnapshot(const std::string* groupFilter = nullptr);
std::vector<std::string> CollectTaskGroups();
}
