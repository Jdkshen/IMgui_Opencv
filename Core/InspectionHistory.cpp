#include "InspectionHistory.h"
#include "SpcDatabase.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <limits>
#include <utility>

namespace
{
std::vector<InspectionHistory::Sample> s_samples;
std::uint64_t s_nextSequence = 1;
constexpr std::size_t kMaximumSamples = 100000;
bool s_databaseLoaded = false;
std::string s_databasePathOverride;

std::string DefaultDatabasePath()
{
    char* localAppData = nullptr;
    std::size_t environmentLength = 0;
    _dupenv_s(&localAppData, &environmentLength, "LOCALAPPDATA");
    std::filesystem::path directory = localAppData && *localAppData
        ? std::filesystem::path(localAppData) / "IMgui_Opencv"
        : std::filesystem::temp_directory_path() / "IMgui_Opencv";
    std::free(localAppData);
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    return (directory / "spc_history.db").string();
}

void EnsureDatabaseLoaded()
{
    if (s_databaseLoaded)
        return;
    s_databaseLoaded = true;
    const std::string path = s_databasePathOverride.empty()
        ? DefaultDatabasePath() : s_databasePathOverride;
    if (!SpcDatabase::Open(path))
        return;
    for (const SpcDatabaseRecord& record : SpcDatabase::LoadRecent(kMaximumSamples))
    {
        s_samples.push_back({record.sequence, record.timestamp, record.toolId,
            record.toolName, record.measurementName, record.value, record.unit,
            record.status});
        s_nextSequence = (std::max)(s_nextSequence, record.sequence + 1);
    }
}

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
void ConfigureDatabase(const std::string& path)
{
    SpcDatabase::Close();
    s_samples.clear();
    s_nextSequence = 1;
    s_databaseLoaded = false;
    s_databasePathOverride = path;
}

const std::vector<Sample>& Samples()
{
    EnsureDatabaseLoaded();
    return s_samples;
}

std::vector<std::string> MeasurementNames()
{
    EnsureDatabaseLoaded();
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
    EnsureDatabaseLoaded();
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
    EnsureDatabaseLoaded();
    s_samples.clear();
    s_nextSequence = 1;
    SpcDatabase::Clear();
}

void Add(Sample sample)
{
    EnsureDatabaseLoaded();
    if (sample.sequence == 0)
        sample.sequence = s_nextSequence++;
    else if (sample.sequence >= s_nextSequence)
        s_nextSequence = sample.sequence + 1;
    if (sample.timestamp.empty())
        sample.timestamp = CurrentTimestamp();
    if (s_samples.size() >= kMaximumSamples)
        s_samples.erase(s_samples.begin(), s_samples.begin() + 1000);
    SpcDatabaseRecord record;
    record.sequence = sample.sequence;
    record.timestamp = sample.timestamp;
    record.toolId = sample.toolId;
    record.toolName = sample.toolName;
    record.measurementName = sample.measurementName;
    record.value = sample.value;
    record.unit = sample.unit;
    record.status = sample.status;
    s_samples.push_back(std::move(sample));
    SpcDatabase::Append(record);
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
    EnsureDatabaseLoaded();
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
    EnsureDatabaseLoaded();
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
