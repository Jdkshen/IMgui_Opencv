#pragma once

#include <cstdarg>
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

class LogSystem
{
public:
    static void Add(LogLevel level, const char* fmt, ...);
    static void Add(LogLevel level, const ImVec4& color, const char* fmt, ...);
    static void Clear();
    static std::shared_ptr<std::vector<LogEntry>> GetLogs();

private:
    static void AddFormatted(LogLevel level, const ImVec4* customColor, const char* fmt, va_list args);

    static inline std::vector<LogEntry> s_logs;
    static inline std::mutex s_mutex;
    static inline std::shared_ptr<std::vector<LogEntry>> s_snapshot;
};
