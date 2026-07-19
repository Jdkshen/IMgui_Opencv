#include "InspectionHistory.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <limits>
#include <utility>

namespace
{
std::vector<InspectionHistory::Sample> s_samples;
std::uint64_t s_nextSequence = 1;
constexpr std::size_t kMaximumSamples = 100000;

std::string CurrentTimestamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &time);
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
        local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
        local.tm_hour, local.tm_min, local.tm_sec);
    return buffer;
}

std::string CsvEscape(const std::string& value)
{
    if (value.find_first_of(",\"\r\n") == std::string::npos)
        return value;

    std::string escaped = "\"";
    for (char valueChar : value)
    {
        if (valueChar == '\"')
            escaped += "\"\"";
        else
            escaped += valueChar;
    }
    escaped += '\"';
    return escaped;
}
}

namespace InspectionHistory
{
const std::vector<Sample>& Samples()
{
    return s_samples;
}

std::vector<std::string> MeasurementNames()
{
    std::vector<std::string> names;
    for (const Sample& sample : s_samples)
    {
        if (sample.measurementName.empty() ||
            std::find(names.begin(), names.end(), sample.measurementName) != names.end())
            continue;
        names.push_back(sample.measurementName);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<double> Trend(const std::string& measurementName,
    std::size_t maximumSamples)
{
    std::vector<double> values;
    for (const Sample& sample : s_samples)
    {
        if (sample.measurementName != measurementName ||
            sample.status == ToolResultStatus::Error || !std::isfinite(sample.value))
            continue;
        values.push_back(sample.value);
    }
    if (maximumSamples > 0 && values.size() > maximumSamples)
        values.erase(values.begin(), values.end() - static_cast<std::ptrdiff_t>(maximumSamples));
    return values;
}

void Clear()
{
    s_samples.clear();
    s_nextSequence = 1;
}

void Add(Sample sample)
{
    if (sample.sequence == 0)
        sample.sequence = s_nextSequence++;
    else if (sample.sequence >= s_nextSequence)
        s_nextSequence = sample.sequence + 1;
    if (sample.timestamp.empty())
        sample.timestamp = CurrentTimestamp();
    if (s_samples.size() >= kMaximumSamples)
        s_samples.erase(s_samples.begin(), s_samples.begin() + 1000);
    s_samples.push_back(std::move(sample));
}

void AddResult(std::uint64_t toolId, const std::string& toolName,
    const ToolResult& result, const std::string& timestamp)
{
    for (const ToolResult::Measurement& measurement : result.measurements)
    {
        Add({
            0, timestamp, toolId, toolName, measurement.name,
            measurement.value, measurement.unit, result.status
        });
    }
}

Statistics Compute(const std::string& measurementName,
    double nominal, double toleranceMinus, double tolerancePlus)
{
    Statistics statistics;
    std::vector<double> values;
    for (const Sample& sample : s_samples)
    {
        if (!measurementName.empty() && sample.measurementName != measurementName)
            continue;
        if (sample.status == ToolResultStatus::Error || !std::isfinite(sample.value))
            continue;
        values.push_back(sample.value);
    }

    statistics.count = values.size();
    statistics.hasTolerance = toleranceMinus > 0.0 || tolerancePlus > 0.0;
    if (statistics.hasTolerance)
    {
        statistics.lowerLimit = nominal - (std::max)(0.0, toleranceMinus);
        statistics.upperLimit = nominal + (std::max)(0.0, tolerancePlus);
    }
    if (values.empty())
    {
        if (statistics.hasTolerance)
            statistics.cp = statistics.cpk = 0.0;
        return statistics;
    }

    statistics.minimum = *std::min_element(values.begin(), values.end());
    statistics.maximum = *std::max_element(values.begin(), values.end());
    for (double value : values)
        statistics.mean += value;
    statistics.mean /= static_cast<double>(values.size());

    double variance = 0.0;
    for (double value : values)
    {
        const double delta = value - statistics.mean;
        variance += delta * delta;
    }
    statistics.standardDeviation = std::sqrt(variance / static_cast<double>(values.size()));

    if (statistics.hasTolerance)
    {
        if (statistics.standardDeviation > std::numeric_limits<double>::epsilon())
        {
            statistics.cp = (statistics.upperLimit - statistics.lowerLimit) /
                (6.0 * statistics.standardDeviation);
            statistics.cpk = (std::min)(
                (statistics.upperLimit - statistics.mean) / (3.0 * statistics.standardDeviation),
                (statistics.mean - statistics.lowerLimit) / (3.0 * statistics.standardDeviation));
        }
        else
        {
            statistics.cp = 0.0;
            statistics.cpk = 0.0;
        }
    }
    return statistics;
}

bool ExportCsv(const char* filepath)
{
    if (!filepath || !*filepath)
        return false;
    std::ofstream output(filepath, std::ios::binary);
    if (!output)
        return false;

    output << "sequence,timestamp,toolId,toolName,measurement,value,unit,status\n";
    for (const Sample& sample : s_samples)
    {
        output << sample.sequence << ','
            << CsvEscape(sample.timestamp) << ','
            << sample.toolId << ','
            << CsvEscape(sample.toolName) << ','
            << CsvEscape(sample.measurementName) << ','
            << sample.value << ','
            << CsvEscape(sample.unit) << ','
            << ToolResultStatusName(sample.status) << '\n';
    }
    return output.good();
}
}
