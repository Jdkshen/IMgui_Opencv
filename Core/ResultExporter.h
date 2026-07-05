#pragma once

#include <string>
#include <vector>

#include <opencv2/core/mat.hpp>

#include "../Algorithm/ToolResult.h"
#include "ToolInstance.h"

namespace ResultExporter
{
    struct ExportSnapshot
    {
        std::string recipeName;
        std::string imagePath;
        int imageWidth = 0;
        int imageHeight = 0;
        int imageVersion = 0;
        std::string resultImagePath;
        float totalTimeMs = 0.0f;
        std::vector<ToolInstance> tools;
        std::vector<float> toolTimesMs;
        std::vector<ToolResult> results;
    };

    std::string ReportsDirectory();
    std::string BuildDefaultOutputPath(const char* prefix, const char* extension);
    bool ExportImageSnapshot(const char* filepath, const cv::Mat& image);
    bool ExportResultsJson(const char* filepath, const ExportSnapshot& snapshot);
    bool ExportRunReportMarkdown(const char* filepath, const ExportSnapshot& snapshot);
}
