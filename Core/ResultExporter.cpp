#include "ResultExporter.h"

#include "../Algorithm/ITool.h"

#include <windows.h>
#include <nlohmann/json.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

using nlohmann::json;

namespace
{
std::string WideToUtf8(const wchar_t* text)
{
    if (!text || !*text)
        return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0)
        return {};
    std::string out((size_t)len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), len, nullptr, nullptr);
    out.resize((size_t)len - 1);
    return out;
}

std::string ExeDir()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring ws(path);
    const size_t slash = ws.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        ws.resize(slash);
    return WideToUtf8(ws.c_str());
}

std::string Timestamp()
{
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04u%02u%02u_%02u%02u%02u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

std::string SafeName(std::string text)
{
    for (char& ch : text)
    {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (std::iscntrl(c) || ch == '\\' || ch == '/' || ch == ':' || ch == '*' ||
            ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|')
            ch = '_';
    }
    text.erase(std::remove(text.begin(), text.end(), '\0'), text.end());
    return text.empty() ? "run" : text;
}

bool WriteTextFile(const char* filepath, const std::string& text)
{
    if (!filepath || !*filepath)
        return false;

    std::filesystem::path path(filepath);
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path());

    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return out.good();
}

json RectToJson(const cv::Rect& r)
{
    return {{"x", r.x}, {"y", r.y}, {"w", r.width}, {"h", r.height}};
}

json PointToJson(const cv::Point& p)
{
    return {{"x", p.x}, {"y", p.y}};
}

json ToolToJson(const ToolInstance& tool, int index, float ms)
{
    const char* name = (tool.type == 12) ? "原图" : ToolRegistry::GetName(tool.type);
    return {
        {"index", index},
        {"type", tool.type},
        {"name", name ? name : ""},
        {"label", tool.label},
        {"displayName", ToolInstanceTitle(name, tool.label)},
        {"timeMs", ms},
        {"searchRoiCount", tool.searchROIs.size()}
    };
}

json ResultToJson(const ToolResult& result)
{
    json j;
    j["toolName"] = result.toolName;
    j["sourceToolIndex"] = result.sourceToolIndex;
    j["sourceToolId"] = result.sourceToolId;
    j["success"] = result.success;
    j["skipped"] = result.skipped;
    j["message"] = result.message;
    j["status"] = ToolResultStatusName(result.status);
    j["statusReason"] = result.statusReason;

    j["measurements"] = json::array();
    for (const auto& m : result.measurements)
        j["measurements"].push_back({{"name", m.name}, {"value", m.value}, {"unit", m.unit}});

    j["regions"] = json::array();
    for (const auto& r : result.regions)
    {
        json region;
        region["bbox"] = RectToJson(r.bbox);
        region["center"] = {{"x", r.center.x}, {"y", r.center.y}};
        region["area"] = r.area;
        region["score"] = r.score;
        region["angle"] = r.angle;
        region["width"] = r.width;
        region["height"] = r.height;
        region["circularity"] = r.circularity;
        region["aspectRatio"] = r.aspectRatio;
        region["label"] = r.label;
        region["contour"] = json::array();
        for (const auto& p : r.contour)
            region["contour"].push_back(PointToJson(p));
        j["regions"].push_back(std::move(region));
    }

    j["detections"] = json::array();
    for (const auto& d : result.detections)
        j["detections"].push_back({{"box", RectToJson(d.box)}, {"classId", d.classId}, {"score", d.score}, {"label", d.label}});

    j["lines"] = json::array();
    for (const auto& l : result.lines)
        j["lines"].push_back({{"p1", PointToJson(l.p1)}, {"p2", PointToJson(l.p2)}, {"length", l.length}, {"angle", l.angle}});

    j["texts"] = json::array();
    for (const auto& t : result.texts)
        j["texts"].push_back({{"text", t.text}, {"box", RectToJson(t.box)}, {"confidence", t.confidence}});

    return j;
}

int CountRegions(const std::vector<ToolResult>& results)
{
    int total = 0;
    for (const auto& r : results)
        total += static_cast<int>(r.regions.size());
    return total;
}

int CountDetections(const std::vector<ToolResult>& results)
{
    int total = 0;
    for (const auto& r : results)
        total += static_cast<int>(r.detections.size());
    return total;
}

int CountLines(const std::vector<ToolResult>& results)
{
    int total = 0;
    for (const auto& r : results)
        total += static_cast<int>(r.lines.size());
    return total;
}

int CountTexts(const std::vector<ToolResult>& results)
{
    int total = 0;
    for (const auto& r : results)
        total += static_cast<int>(r.texts.size());
    return total;
}
}

namespace ResultExporter
{
std::string ReportsDirectory()
{
    return ExeDir() + "\\reports";
}

std::string BuildDefaultOutputPath(const char* prefix, const char* extension)
{
    const std::string dir = ReportsDirectory();
    const std::string safePrefix = SafeName(prefix && *prefix ? prefix : "run");
    const std::string ext = extension && *extension ? extension : "txt";
    return dir + "\\" + safePrefix + "_" + Timestamp() + "." + ext;
}

bool ExportImageSnapshot(const char* filepath, const cv::Mat& image)
{
    if (!filepath || !*filepath || image.empty())
        return false;

    std::filesystem::path path(filepath);
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path());
    return cv::imwrite(filepath, image);
}

bool ExportResultsJson(const char* filepath, const ExportSnapshot& snapshot)
{
    json j;
    j["version"] = 1;
    j["kind"] = "vision_results";
    j["recipeName"] = snapshot.recipeName;
    j["imagePath"] = snapshot.imagePath;
    j["resultImagePath"] = snapshot.resultImagePath;
    j["image"] = {{"width", snapshot.imageWidth}, {"height", snapshot.imageHeight}, {"version", snapshot.imageVersion}};
    j["totalTimeMs"] = snapshot.totalTimeMs;

    j["summary"] = {
        {"toolCount", snapshot.tools.size()},
        {"resultCount", snapshot.results.size()},
        {"regions", CountRegions(snapshot.results)},
        {"detections", CountDetections(snapshot.results)},
        {"lines", CountLines(snapshot.results)},
        {"texts", CountTexts(snapshot.results)}
    };

    j["tools"] = json::array();
    for (size_t i = 0; i < snapshot.tools.size(); ++i)
    {
        const float ms = i < snapshot.toolTimesMs.size() ? snapshot.toolTimesMs[i] : 0.0f;
        j["tools"].push_back(ToolToJson(snapshot.tools[i], static_cast<int>(i), ms));
    }

    j["results"] = json::array();
    for (const auto& result : snapshot.results)
        j["results"].push_back(ResultToJson(result));

    return WriteTextFile(filepath, j.dump(2));
}

bool ExportRunReportMarkdown(const char* filepath, const ExportSnapshot& snapshot)
{
    std::ostringstream out;
    out << "# 运行报告\n\n";
    out << "- 配方: " << (snapshot.recipeName.empty() ? "未命名" : snapshot.recipeName) << "\n";
    out << "- 图片: " << (snapshot.imagePath.empty() ? "未记录" : snapshot.imagePath) << "\n";
    if (!snapshot.resultImagePath.empty())
        out << "- 结果图像: " << snapshot.resultImagePath << "\n";
    out << "- 尺寸: " << snapshot.imageWidth << "x" << snapshot.imageHeight << "\n";
    out << "- 总耗时: " << snapshot.totalTimeMs << " ms\n";
    out << "- 工具数: " << snapshot.tools.size() << "\n";
    out << "- 结果数: " << snapshot.results.size() << "\n\n";

    out << "## 工具耗时\n\n";
    out << "| # | 工具 | 类型 | 耗时(ms) | ROI |\n";
    out << "|---|------|------|----------|-----|\n";
    for (size_t i = 0; i < snapshot.tools.size(); ++i)
    {
        const auto& tool = snapshot.tools[i];
        const char* name = (tool.type == 12) ? "原图" : ToolRegistry::GetName(tool.type);
        const float ms = i < snapshot.toolTimesMs.size() ? snapshot.toolTimesMs[i] : 0.0f;
        out << "| " << (i + 1) << " | " << ToolInstanceTitle(name, tool.label)
            << " | " << tool.type << " | " << ms << " | " << tool.searchROIs.size() << " |\n";
    }

    out << "\n## 结果摘要\n\n";
    out << "| 工具 | 状态 | 区域 | 检测 | 线段 | 文本 | 测量 | 消息 |\n";
    out << "|------|------|------|------|------|------|------|------|\n";
    for (const auto& result : snapshot.results)
    {
        out << "| " << result.toolName
            << " | " << ToolResultStatusName(result.status)
            << " | " << result.regions.size()
            << " | " << result.detections.size()
            << " | " << result.lines.size()
            << " | " << result.texts.size()
            << " | " << result.measurements.size()
            << " | " << (result.statusReason.empty() ? result.message : result.statusReason)
            << " |\n";
    }

    out << "\n## 详细结果\n\n";
    for (const auto& result : snapshot.results)
    {
        out << "### " << result.toolName << "\n\n";
        for (const auto& r : result.regions)
            out << "- 区域 `" << r.label << "` bbox=(" << r.bbox.x << "," << r.bbox.y << "," << r.bbox.width << "," << r.bbox.height << ") score=" << r.score << "\n";
        for (const auto& d : result.detections)
            out << "- 检测 `" << d.label << "` box=(" << d.box.x << "," << d.box.y << "," << d.box.width << "," << d.box.height << ") score=" << d.score << "\n";
        for (const auto& l : result.lines)
            out << "- 线段 (" << l.p1.x << "," << l.p1.y << ")->(" << l.p2.x << "," << l.p2.y << ") length=" << l.length << " angle=" << l.angle << "\n";
        for (const auto& t : result.texts)
            out << "- 文本 `" << t.text << "` box=(" << t.box.x << "," << t.box.y << "," << t.box.width << "," << t.box.height << ") confidence=" << t.confidence << "\n";
        for (const auto& measurement : result.measurements)
            out << "- 测量 `" << measurement.name << "` = " << measurement.value << " " << measurement.unit << "\n";
        if (result.regions.empty() && result.detections.empty() && result.lines.empty() && result.texts.empty() && result.measurements.empty())
            out << "- 无几何结果\n";
        out << "\n";
    }

    return WriteTextFile(filepath, out.str());
}
}
