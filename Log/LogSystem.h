#pragma once

#include <cstdarg>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "imgui/imgui.h"

enum LogLevel
{
    LOG_INFO = 0,
    LOG_WARN = 1,
    LOG_ERROR = 2
};

struct LogEntry
{
    std::string displayText;
    LogLevel level = LOG_INFO;
    int threadId = 0;
    ImVec4 color = ImVec4(1, 1, 1, 1);
    bool useCustomColor = false;
    double timestamp = 0.0;
};

struct LogContext
{
    std::string taskId;
    std::string batchNumber;
    std::string imageName;
    std::string plcRequestId;

    bool Empty() const
    {
        return taskId.empty() && batchNumber.empty() && imageName.empty() &&
            plcRequestId.empty();
    }
};

struct LogStorageOptions
{
    std::string directory;
    std::uint64_t maximumFileBytes = 10u * 1024u * 1024u;
    int retentionDays = 30;
};

class LogSystem
{
public:
    static void Add(LogLevel level, const char* fmt, ...);
    static void Add(LogLevel level, const ImVec4& color, const char* fmt, ...);
    static void Clear();
    static std::shared_ptr<std::vector<LogEntry>> GetLogs();
    static void ConfigureStorage(const LogStorageOptions& options);
    static std::string GetLogDirectory();
    static void SetContext(const LogContext& context);
    static LogContext GetContext();
    static void ClearContext();
    static bool ExportDiagnosticPackage(const std::string& targetPath,
        std::string* error = nullptr);

private:
    static void AddFormatted(LogLevel level, const ImVec4* customColor, const char* fmt, va_list args);

    static inline std::vector<LogEntry> s_logs;
    static inline std::mutex s_mutex;
    static inline std::shared_ptr<std::vector<LogEntry>> s_snapshot;
};

class ScopedLogContext
{
public:
    explicit ScopedLogContext(LogContext context)
        : previous_(LogSystem::GetContext())
    {
        LogSystem::SetContext(context);
    }

    ~ScopedLogContext()
    {
        LogSystem::SetContext(previous_);
    }

    ScopedLogContext(const ScopedLogContext&) = delete;
    ScopedLogContext& operator=(const ScopedLogContext&) = delete;

private:
    LogContext previous_;
};
