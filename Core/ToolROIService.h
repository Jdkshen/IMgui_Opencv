#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <opencv2/core/types.hpp>

#include "ToolInstance.h"

struct ToolROIEditResult
{
    bool success = false;
    std::string message;
    cv::Rect bounds;
};

namespace ToolROIService
{
    int BeginSearchROIEdit(ToolInstance& tool);
    int ActiveSearchROIIndex(std::uint64_t toolId);
    bool IsSearchROIEditActive(std::uint64_t toolId);
    ToolROIEditResult ConfirmSearchROIEdit(ToolInstance& tool);
    void CancelSearchROIEdit(std::uint64_t toolId);
    void ClearSearchROIs(ToolInstance& tool);

    bool SyncMeasurementROIs(ToolInstance& tool);
    void RestoreMeasurementROIs(ToolInstance& tool);
    void RemoveMeasurementROIs(ToolInstance& tool);
    void RestoreMeasurementROIBackup(ToolInstance& tool, const std::vector<ROI>& backup);
    bool SelectMeasurementROI(const ToolInstance& tool, std::size_t ordinal = 0);

    void ForgetTool(std::uint64_t toolId);
    void ClearSessions();
}
