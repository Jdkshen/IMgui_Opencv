#pragma once

#include "../Algorithm/ToolResult.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace InspectionHistory
{
    struct Sample
    {
        std::uint64_t sequence = 0;
        std::string timestamp;
        std::uint64_t toolId = 0;
        std::string toolName;
        std::string measurementName;
        double value = 0.0;
        std::string unit;
        ToolResultStatus status = ToolResultStatus::Pass;
    };

    struct Statistics
    {
        std::size_t count = 0;
        double mean = 0.0;
        double standardDeviation = 0.0;
        double minimum = 0.0;
        double maximum = 0.0;
        double cp = -1.0;
        double cpk = -1.0;
        bool hasTolerance = false;
        double lowerLimit = 0.0;
        double upperLimit = 0.0;
    };

    const std::vector<Sample>& Samples();
    void ConfigureDatabase(const std::string& path);
    std::vector<std::string> MeasurementNames();
    std::vector<double> Trend(const std::string& measurementName,
        std::size_t maximumSamples = 0);
    void Clear();
    void Add(Sample sample);
    void AddResult(std::uint64_t toolId, const std::string& toolName,
        const ToolResult& result, const std::string& timestamp = {});
    Statistics Compute(const std::string& measurementName = {},
        double nominal = 0.0, double toleranceMinus = 0.0,
        double tolerancePlus = 0.0);
    bool ExportCsv(const char* filepath);
}
