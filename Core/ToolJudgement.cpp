#include "ToolJudgement.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <vector>

namespace
{
void AppendReason(std::string& reason, const std::string& item)
{
    if (!reason.empty())
        reason += "; ";
    reason += item;
}

std::string NormalizeText(std::string text, bool caseSensitive)
{
    if (!caseSensitive)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char value)
        {
            return static_cast<char>(std::tolower(value));
        });
    }
    return text;
}

std::vector<std::string> ResultTexts(const ToolResult& result)
{
    std::vector<std::string> texts;
    for (const auto& item : result.texts)
        texts.push_back(item.text);
    for (const auto& item : result.detections)
        texts.push_back(item.label);
    for (const auto& item : result.regions)
        texts.push_back(item.label);
    return texts;
}

float BestScore(const ToolResult& result)
{
    float best = -1.0f;
    for (const auto& item : result.detections)
        best = (std::max)(best, item.score);
    for (const auto& item : result.regions)
        best = (std::max)(best, item.score);
    for (const auto& item : result.texts)
        best = (std::max)(best, item.confidence);
    return best;
}

float LargestArea(const ToolResult& result)
{
    float largest = -1.0f;
    for (const auto& item : result.regions)
    {
        const float area = item.area > 0.0f ? item.area : static_cast<float>(item.bbox.area());
        largest = (std::max)(largest, area);
    }
    for (const auto& item : result.detections)
        largest = (std::max)(largest, static_cast<float>(item.box.area()));
    for (const auto& item : result.texts)
        largest = (std::max)(largest, static_cast<float>(item.box.area()));
    return largest;
}

const ToolResult::Measurement* FindMeasurement(const ToolResult& result, const std::string& name)
{
    const auto found = std::find_if(result.measurements.begin(), result.measurements.end(),
        [&name](const ToolResult::Measurement& measurement)
        {
            return measurement.name == name;
        });
    return found == result.measurements.end() ? nullptr : &*found;
}
}

namespace ToolJudgement
{
int PrimaryResultCount(const ToolResult& result)
{
    if (!result.texts.empty())
        return static_cast<int>(result.texts.size());
    if (!result.detections.empty())
        return static_cast<int>(result.detections.size());
    if (!result.regions.empty())
        return static_cast<int>(result.regions.size());
    if (!result.lines.empty())
        return static_cast<int>(result.lines.size());
    if (!result.measurements.empty())
        return static_cast<int>(result.measurements.size());
    return 0;
}

void Evaluate(ToolResult& result, const ToolJudgementSettings& settings)
{
    if (!result.success)
    {
        result.status = ToolResultStatus::Error;
        result.statusReason = result.message.empty() ? "工具执行失败" : result.message;
        return;
    }

    const bool intrinsicFailure = result.status == ToolResultStatus::Fail;
    if (!intrinsicFailure)
        result.statusReason.clear();
    result.status = intrinsicFailure ? ToolResultStatus::Fail : ToolResultStatus::Pass;
    if (!settings.enabled)
        return;

    const int count = PrimaryResultCount(result);
    if (count < (std::max)(0, settings.minResultCount))
        AppendReason(result.statusReason, "结果数量低于下限");
    if (settings.maxResultCount >= 0 && count > settings.maxResultCount)
        AppendReason(result.statusReason, "结果数量高于上限");

    if (settings.minScore >= 0.0f)
    {
        const float score = BestScore(result);
        if (score < settings.minScore)
            AppendReason(result.statusReason, "最高分数低于下限");
    }

    const float area = LargestArea(result);
    if (settings.minArea >= 0.0f && area < settings.minArea)
        AppendReason(result.statusReason, "最大面积低于下限");
    if (settings.maxArea >= 0.0f && area > settings.maxArea)
        AppendReason(result.statusReason, "最大面积高于上限");

    if (settings.measurementRangeEnabled)
    {
        const ToolResult::Measurement* measurement = FindMeasurement(result, settings.measurementName);
        if (!measurement)
        {
            AppendReason(result.statusReason, settings.measurementName.empty()
                ? "未配置测量项"
                : "未找到测量项: " + settings.measurementName);
        }
        else
        {
            const double lower = (std::min)(settings.minMeasurement, settings.maxMeasurement);
            const double upper = (std::max)(settings.minMeasurement, settings.maxMeasurement);
            if (measurement->value < lower)
                AppendReason(result.statusReason, settings.measurementName + " 低于下限");
            if (measurement->value > upper)
                AppendReason(result.statusReason, settings.measurementName + " 高于上限");
        }
    }

    if (!settings.requiredText.empty())
    {
        const std::string expected = NormalizeText(settings.requiredText, settings.textCaseSensitive);
        bool matched = false;
        for (const std::string& value : ResultTexts(result))
        {
            const std::string actual = NormalizeText(value, settings.textCaseSensitive);
            matched = settings.textMatchMode == 1
                ? actual == expected
                : actual.find(expected) != std::string::npos;
            if (matched)
                break;
        }
        if (!matched)
            AppendReason(result.statusReason, "文本条件不匹配");
    }

    if (!result.statusReason.empty())
        result.status = ToolResultStatus::Fail;
}

bool ShouldStop(const ToolResult& result, const ToolJudgementSettings& settings)
{
    return settings.stopOnFailure && result.status != ToolResultStatus::Pass;
}
}
