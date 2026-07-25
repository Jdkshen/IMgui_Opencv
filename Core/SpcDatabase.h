#pragma once

#include "../Algorithm/ToolResult.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct SpcDatabaseRecord
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

struct SpcDatabaseSnapshot
{
    bool available = false;
    bool open = false;
    std::string path;
    std::string lastError;
    std::uint64_t insertedRecords = 0;
};

namespace SpcDatabase
{
    bool Open(const std::string& path);
    void Close();
    bool Append(const SpcDatabaseRecord& record);
    std::vector<SpcDatabaseRecord> LoadRecent(std::size_t maximumRecords);
    bool Clear();
    SpcDatabaseSnapshot Snapshot();
}
