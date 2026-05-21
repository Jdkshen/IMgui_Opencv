#include "LogSystem.h"
#include <cstdarg>
#include <cstdio>
#include <chrono>
#include <ctime>
#include <mutex>

// =========================
// 全局互斥锁：保护日志容器的线程安全
// =========================
static std::mutex g_logMutex;

std::vector<LogItem> LogSystem::s_logs;

// =========================
// 获取当前时间字符串（格式：YYYY-MM-DD HH:mm:ss.SSS）
// =========================
static std::string GetTimeString()
{
    using namespace std::chrono;

    auto now = system_clock::now();
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    std::time_t t = system_clock::to_time_t(now);

    std::tm tm{};
    localtime_s(&tm, &t);

    char buffer[64];
    snprintf(buffer, sizeof(buffer),
        "%04d-%02d-%02d %02d:%02d:%02d.%03d",
        tm.tm_year + 1900,
        tm.tm_mon + 1,
        tm.tm_mday,
        tm.tm_hour,
        tm.tm_min,
        tm.tm_sec,
        (int)ms.count()
    );

    return buffer;
}

// =========================
// 线程安全的日志添加（带自定义颜色）
// =========================
void LogSystem::Add(LogLevel level, const ImVec4& color, const char* fmt, ...)
{
    char buffer[1024];

    va_list args;
    va_start(args, fmt);
    vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, fmt, args);
    va_end(args);

    LogItem item;
    item.level = level;
    item.time = GetTimeString();
    item.text = buffer;
    item.color = color;
    item.useCustomColor = true;

    std::lock_guard<std::mutex> lock(g_logMutex);
    s_logs.push_back(std::move(item));

    // 防止日志无限增长（最多保留2000条）
    if (s_logs.size() > 2000)
        s_logs.erase(s_logs.begin());
}

// =========================
// 线程安全的日志添加（默认颜色）
// =========================
void LogSystem::Add(LogLevel level, const char* fmt, ...)
{
    char buffer[1024];

    va_list args;
    va_start(args, fmt);
    vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, fmt, args);
    va_end(args);

    LogItem item;
    item.level = level;
    item.time = GetTimeString();
    item.text = buffer;
    item.useCustomColor = false;

    std::lock_guard<std::mutex> lock(g_logMutex);
    s_logs.push_back(std::move(item));

    if (s_logs.size() > 2000)
        s_logs.erase(s_logs.begin());
}

// =========================
// 清空所有日志
// =========================
void LogSystem::Clear()
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    s_logs.clear();
}

// =========================
// 获取日志列表（返回副本，保证线程安全）
// =========================
std::vector<LogItem> LogSystem::GetLogs()
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    return s_logs;
}

// =========================
// 根据线程ID生成颜色，同一线程日志颜色一致
// 用于并发日志测试中区分不同线程的输出
// =========================
ImVec4 LogSystem::GetThreadColor()
{
    auto id = std::this_thread::get_id();
    size_t h = std::hash<std::thread::id>{}(id);

    float r = ((h & 0xFF) / 255.0f);
    float g = (((h >> 8) & 0xFF) / 255.0f);
    float b = (((h >> 16) & 0xFF) / 255.0f);

    return ImVec4(r, g, b, 1.0f);
}


