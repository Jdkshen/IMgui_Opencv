#pragma once

enum class ToolSpatialResultChannel : int
{
    Auto = 0,
    Regions = 1,
    Detections = 2,
    Lines = 3,
    Texts = 4,
};

// Static description of the ToolResult channels a tool type may publish.
// Runtime results can still contain fewer channels (for example, no matches),
// but UI source selection and preflight validation must use the same contract.
struct ToolResultCapabilities
{
    bool regions = false;
    bool detections = false;
    bool lines = false;
    bool texts = false;
    bool measurements = false;
    bool processedImage = false;

    constexpr bool SupportsSpatialResult() const
    {
        return regions || detections || lines || texts;
    }

    constexpr bool SupportsChannel(ToolSpatialResultChannel channel) const
    {
        switch (channel)
        {
        case ToolSpatialResultChannel::Auto: return SupportsSpatialResult();
        case ToolSpatialResultChannel::Regions: return regions;
        case ToolSpatialResultChannel::Detections: return detections;
        case ToolSpatialResultChannel::Lines: return lines;
        case ToolSpatialResultChannel::Texts: return texts;
        default: return false;
        }
    }
};

constexpr bool IsValidSpatialResultChannel(int channel)
{
    return channel >= static_cast<int>(ToolSpatialResultChannel::Auto) &&
        channel <= static_cast<int>(ToolSpatialResultChannel::Texts);
}

constexpr ToolResultCapabilities ToolCapabilitiesForType(int type)
{
    switch (type)
    {
    case 0:  return {false, false, false, false, false, true};
    case 1:  return {true,  false, false, false, true,  false};
    case 2:  return {true,  false, false, false, true,  false};
    case 3:  return {false, false, false, false, false, true};
    case 4:  return {false, true,  false, false, false, false};
    case 5:  return {true,  false, false, false, true,  false};
    case 6:  return {true,  false, false, false, false, false};
    case 7:  return {false, false, true,  false, false, false};
    case 8:  return {false, false, false, false, false, true};
    case 9:  return {false, false, false, false, true,  true};
    case 10: return {true,  false, false, false, false, false};
    case 11: return {false, true,  false, false, false, false};
    case 12: return {false, false, false, false, false, true};
    case 13: return {false, false, false, true,  true,  false};
    case 14: return {true,  false, false, true,  true,  false};
    case 15: return {true,  false, true,  false, true,  false};
    case 16: return {true,  false, false, false, true,  true};
    case 17: return {true,  false, true,  true,  true,  true};
    default: return {};
    }
}

inline const char* ToolResultKindsLabel(int type)
{
    switch (type)
    {
    case 0:  return "处理图";
    case 1:  return "区域/测量";
    case 2:  return "区域/测量";
    case 3:  return "处理图";
    case 4:  return "检测框";
    case 5:  return "区域/测量";
    case 6:  return "区域";
    case 7:  return "线段";
    case 8:  return "处理图";
    case 9:  return "测量/处理图";
    case 10: return "区域";
    case 11: return "检测框";
    case 12: return "图像";
    case 13: return "文本框/测量";
    case 14: return "区域/文本框/测量";
    case 15: return "区域/线段/测量";
    case 16: return "区域/测量/处理图";
    case 17: return "区域/线段/文本/测量/处理图";
    default: return "未知输出";
    }
}
