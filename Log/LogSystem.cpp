#include "LogSystem.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>

void LogSystem::AddFormatted(LogLevel level, const ImVec4* customColor, const char* fmt, va_list args)
{
    char buf[2048];
    vsnprintf(buf, sizeof(buf), fmt, args);

    auto now = std::chrono::system_clock::now();
    auto timeNow = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm localTm{};
    localtime_s(&localTm, &timeNow);

    char timeStr[32];
    snprintf(timeStr, sizeof(timeStr), "[%02d:%02d:%02d.%03lld]",
        localTm.tm_hour, localTm.tm_min, localTm.tm_sec,
        static_cast<long long>(ms.count()));

    const char* levelStr = "INFO";
    if (level == LOG_WARN)
        levelStr = "WARN";
    if (level == LOG_ERROR)
        levelStr = "ERROR";

    LogEntry entry;
    entry.displayText = std::string(timeStr) + " [" + levelStr + "] " + buf;
    entry.level = level;
    entry.threadId = 0;
    entry.timestamp = static_cast<double>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count()) / 1000.0;
    if (customColor)
    {
        entry.color = *customColor;
        entry.useCustomColor = true;
    }

    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_logs.size() >= 2000)
            s_logs.erase(s_logs.begin(), s_logs.begin() + (s_logs.size() - 1999));
        s_logs.push_back(std::move(entry));
        s_snapshot.reset();
    }
}

void LogSystem::Add(LogLevel level, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    AddFormatted(level, nullptr, fmt, args);
    va_end(args);
}

void LogSystem::Add(LogLevel level, const ImVec4& color, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    AddFormatted(level, &color, fmt, args);
    va_end(args);
}

void LogSystem::Clear()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_logs.clear();
    s_snapshot.reset();
}

std::shared_ptr<std::vector<LogEntry>> LogSystem::GetLogs()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_snapshot)
        s_snapshot = std::make_shared<std::vector<LogEntry>>(s_logs);
    return s_snapshot;
}
