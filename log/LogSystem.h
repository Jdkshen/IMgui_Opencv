#pragma once
#include "../Windows_imgui.h"
#include <string>
#include <vector>
#include <memory>

// =========================
// 日志级别枚举
// =========================
enum LogLevel
{
    LOG_INFO,    // 信息
    LOG_WARN,    // 警告
    LOG_ERROR    // 错误
};

// =========================
// 日志条目结构体
// =========================
struct LogItem
{
    LogLevel level;              // 日志级别
    std::string time;            // 时间戳（短格式 HH:MM:SS.mmm）
    std::string text;            // 日志内容
    std::string displayText;     // ⭐预格式化：Add()时一次性生成，渲染零开销

    ImVec4 color;                // 自定义颜色
    bool useCustomColor = false; // 是否使用自定义颜色
};

// =========================
// LogSystem 日志系统类
// =========================
class LogSystem
{
public:
    // 添加日志（带颜色）
    static void Add(LogLevel level, const char* fmt, ...);
    // 添加日志（带自定义颜色）
    static void Add(LogLevel level, const ImVec4& color, const char* fmt, ...);
    // 清空所有日志
    static void Clear();
    // 获取日志列表（shared_ptr COW，零拷贝，线程安全）
    static std::shared_ptr<const std::vector<LogItem>> GetLogs();
    // 获取基于线程ID的颜色
    static ImVec4 GetThreadColor();

private:
    static std::shared_ptr<std::vector<LogItem>> s_logs;  // 日志存储容器（COW）
};