#include "RunResultPresentation.h"

#include "../Algorithm/ITool.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace UI::RunResultPresentation
{
    const char* StatusText(ToolResultStatus status)
    {
        switch (status)
        {
        case ToolResultStatus::Pass: return "OK";
        case ToolResultStatus::Fail: return "NG";
        case ToolResultStatus::Error: return "ERROR";
        default: return "ERROR";
        }
    }

    const char* StatusDescription(ToolResultStatus status)
    {
        switch (status)
        {
        case ToolResultStatus::Pass: return "检测通过";
        case ToolResultStatus::Fail: return "检测不合格";
        case ToolResultStatus::Error: return "执行异常";
        default: return "执行异常";
        }
    }

    ImVec4 StatusColor(ToolResultStatus status, bool dark)
    {
        switch (status)
        {
        case ToolResultStatus::Pass:
            return dark ? ImVec4(0.12f, 0.52f, 0.29f, 1.0f) : ImVec4(0.13f, 0.62f, 0.32f, 1.0f);
        case ToolResultStatus::Fail:
            return dark ? ImVec4(0.72f, 0.22f, 0.18f, 1.0f) : ImVec4(0.82f, 0.24f, 0.18f, 1.0f);
        case ToolResultStatus::Error:
        default:
            return dark ? ImVec4(0.78f, 0.48f, 0.12f, 1.0f) : ImVec4(0.88f, 0.50f, 0.10f, 1.0f);
        }
    }

    ImVec4 StatusTextColor(ToolResultStatus status, bool dark)
    {
        switch (status)
        {
        case ToolResultStatus::Pass:
            return dark ? ImVec4(0.32f, 0.86f, 0.48f, 1.0f) : ImVec4(0.05f, 0.48f, 0.20f, 1.0f);
        case ToolResultStatus::Fail:
            return dark ? ImVec4(1.00f, 0.42f, 0.36f, 1.0f) : ImVec4(0.78f, 0.16f, 0.12f, 1.0f);
        case ToolResultStatus::Error:
        default:
            return dark ? ImVec4(1.00f, 0.70f, 0.28f, 1.0f) : ImVec4(0.72f, 0.36f, 0.04f, 1.0f);
        }
    }

    std::string ToolDisplayName(const ToolInstance& tool)
    {
        const char* baseName = tool.type == 12 ? "原图" : ToolRegistry::GetName(tool.type);
        if (!baseName || !*baseName)
            baseName = "工具";
        if (tool.label.empty())
            return baseName;
        return tool.label;
    }

    std::string ResultSummary(const ToolResult& result)
    {
        if (result.skipped)
            return result.message.empty() ? "已跳过" : result.message;
        std::string summary = result.statusReason.empty()
            ? result.message : result.statusReason;
        auto AppendCount = [&summary](const char* name, std::size_t count)
        {
            if (count == 0)
                return;
            if (!summary.empty())
                summary += " · ";
            summary += name;
            summary += " ";
            summary += std::to_string(count);
        };
        AppendCount("检测", result.detections.size());
        AppendCount("区域", result.regions.size());
        AppendCount("线段", result.lines.size());
        AppendCount("文本", result.texts.size());
        AppendCount("测量", result.measurements.size());
        if (!result.debugImage.empty() || result.timing.debugImageBytes > 0)
            AppendCount("处理图", 1);
        return summary.empty() ? "执行完成" : summary;
    }

    std::string ResultDetails(const ToolResult& result)
    {
        std::ostringstream details;
        details << "状态: " << (result.skipped ? "跳过" : StatusText(result.status));
        if (!result.statusReason.empty())
            details << "\n原因: " << result.statusReason;
        if (!result.message.empty() && result.message != result.statusReason)
            details << "\n说明: " << result.message;

        const float wallMs = result.timing.wallMs > 0.0f
            ? result.timing.wallMs
            : result.timing.prepareMs + result.timing.executeMs + result.timing.publishMs;
        details << std::fixed << std::setprecision(3)
            << "\n耗时: 总 " << wallMs << " ms｜准备 " << result.timing.prepareMs
            << "｜执行 " << result.timing.executeMs << "｜发布 "
            << result.timing.publishMs;

        constexpr std::size_t kMaximumDetailItems = 12;
        const auto AppendOmitted = [&details](std::size_t size)
        {
            if (size > kMaximumDetailItems)
                details << "\n  …其余 " << size - kMaximumDetailItems << " 项";
        };

        if (!result.detections.empty())
        {
            details << "\n检测框 (" << result.detections.size() << ")";
            for (std::size_t index = 0;
                index < (std::min)(result.detections.size(), kMaximumDetailItems); ++index)
            {
                const auto& item = result.detections[index];
                details << "\n  " << index + 1 << ". "
                    << (item.label.empty() ? "未分类" : item.label)
                    << "｜类别 " << item.classId << "｜分数 " << item.score
                    << "｜框 [" << item.box.x << ',' << item.box.y << ','
                    << item.box.width << ',' << item.box.height << ']';
            }
            AppendOmitted(result.detections.size());
        }
        if (!result.regions.empty())
        {
            details << "\n区域 (" << result.regions.size() << ")";
            for (std::size_t index = 0;
                index < (std::min)(result.regions.size(), kMaximumDetailItems); ++index)
            {
                const auto& item = result.regions[index];
                const cv::Point2f center = item.center != cv::Point2f()
                    ? item.center
                    : cv::Point2f(item.bbox.x + item.bbox.width * 0.5f,
                        item.bbox.y + item.bbox.height * 0.5f);
                details << "\n  " << index + 1 << ". "
                    << (item.label.empty() ? "未命名" : item.label)
                    << "｜中心 (" << center.x << ',' << center.y << ")｜面积 "
                    << item.area << "｜分数 " << item.score << "｜角度 " << item.angle;
            }
            AppendOmitted(result.regions.size());
        }
        if (!result.lines.empty())
        {
            details << "\n线段 (" << result.lines.size() << ")";
            for (std::size_t index = 0;
                index < (std::min)(result.lines.size(), kMaximumDetailItems); ++index)
            {
                const auto& item = result.lines[index];
                details << "\n  " << index + 1 << ". (" << item.p1.x << ',' << item.p1.y
                    << ")->(" << item.p2.x << ',' << item.p2.y << ")｜长度 "
                    << item.length << "｜角度 " << item.angle;
            }
            AppendOmitted(result.lines.size());
        }
        if (!result.texts.empty())
        {
            details << "\n文本 (" << result.texts.size() << ")";
            for (std::size_t index = 0;
                index < (std::min)(result.texts.size(), kMaximumDetailItems); ++index)
            {
                const auto& item = result.texts[index];
                details << "\n  " << index + 1 << ". "
                    << (item.text.empty() ? "<空文本>" : item.text)
                    << "｜置信度 " << item.confidence << "｜框 ["
                    << item.box.x << ',' << item.box.y << ',' << item.box.width
                    << ',' << item.box.height << ']';
            }
            AppendOmitted(result.texts.size());
        }
        if (!result.measurements.empty())
        {
            details << "\n测量 (" << result.measurements.size() << ")";
            for (std::size_t index = 0;
                index < (std::min)(result.measurements.size(), kMaximumDetailItems); ++index)
            {
                const auto& item = result.measurements[index];
                details << "\n  " << index + 1 << ". "
                    << (item.name.empty() ? "数值" : item.name) << " = "
                    << item.value;
                if (!item.unit.empty())
                    details << ' ' << item.unit;
            }
            AppendOmitted(result.measurements.size());
        }
        if (!result.debugImage.empty())
        {
            details << "\n处理图: " << result.debugImage.cols << 'x'
                << result.debugImage.rows << "｜通道 " << result.debugImage.channels();
        }
        return details.str();
    }
}
