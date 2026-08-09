#pragma once

#include <cstdio>
#include <string>

#include "../Algorithm/ToolResult.h"

inline bool ToolResultLabelStartsWithToken(const std::string& text, const std::string& token)
{
    if (text.size() < token.size() || token.empty())
        return false;
    if (text.compare(0, token.size(), token) != 0)
        return false;
    if (text.size() == token.size())
        return true;
    const char c = text[token.size()];
    return c == ' ' || c == '[' || c == '#' || c == ':' || c == '-';
}

inline bool ToolResultTextEqualsLabel(const std::string& text, const std::string& label)
{
    if (text.empty() || label.empty())
        return false;
    if (text == label)
        return true;
    return ToolResultLabelStartsWithToken(text, label) ||
        ToolResultLabelStartsWithToken(label, text);
}

inline bool ToolResultHasDuplicateTextLabel(const ToolResult& result, const std::string& itemLabel)
{
    for (const auto& text : result.texts)
    {
        if (ToolResultTextEqualsLabel(text.text, itemLabel))
            return true;
    }
    return false;
}

inline std::string StripLeadingToolLabel(const ToolResult& result, const std::string& itemLabel)
{
    if (itemLabel.empty())
        return itemLabel;

    const std::string& toolName = result.toolName;
    const size_t bracketOpen = toolName.find('[');
    const size_t bracketClose = toolName.find(']', bracketOpen == std::string::npos ? 0 : bracketOpen + 1);
    if (bracketOpen == std::string::npos || bracketClose == std::string::npos || bracketClose <= bracketOpen + 1)
        return itemLabel;

    const std::string label = toolName.substr(bracketOpen + 1, bracketClose - bracketOpen - 1);
    if (!ToolResultLabelStartsWithToken(itemLabel, label))
        return itemLabel;

    size_t pos = label.size();
    while (pos < itemLabel.size() && itemLabel[pos] == ' ')
        ++pos;
    return itemLabel.substr(pos);
}

inline std::string BuildToolResultOverlayLabel(const ToolResult& result, const std::string& itemLabel)
{
    return StripLeadingToolLabel(result, itemLabel);
}

inline std::string BuildToolResultLineOverlayLabel(const ToolResult& result)
{
    std::string detail = result.message;
    for (const ToolResult::Measurement& measurement : result.measurements)
    {
        if (measurement.name != "value")
            continue;
        char value[96]{};
        std::snprintf(value, sizeof(value), "%.3f%s%s", measurement.value,
            measurement.unit.empty() ? "" : " ", measurement.unit.c_str());
        detail = value;
        break;
    }

    if (result.toolName.empty())
        return detail;
    if (detail.empty() || result.toolName.find(detail) != std::string::npos)
        return result.toolName;
    return result.toolName + " " + detail;
}
