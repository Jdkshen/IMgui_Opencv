#pragma once

#include <algorithm>
#include <string>

#include "ToolChainState.h"
#include "ToolResultUtils.h"

namespace ResultOverlayState
{
    struct Settings
    {
        bool showLabels = true;
        bool avoidLabelOverlap = true;
        int maxVisibleLabels = 30;
    };

    inline Settings& MutableSettings()
    {
        static Settings settings;
        return settings;
    }

    inline const Settings& ReadOnlySettings()
    {
        return MutableSettings();
    }

    inline bool ShouldDrawResultLabels(const ToolResult& result)
    {
        const Settings& settings = ReadOnlySettings();
        if (!settings.showLabels)
            return false;
        if (result.sourceToolIndex < 0)
            return true;

        const auto& tools = ToolChainState::ReadOnlyTools();
        if (result.sourceToolIndex >= static_cast<int>(tools.size()))
            return true;
        return tools[result.sourceToolIndex].showResultLabels;
    }

    inline int MaxVisibleLabels()
    {
        return (std::max)(0, ReadOnlySettings().maxVisibleLabels);
    }

    inline bool ShouldDrawRegionLabel(const ToolResult& result, const std::string& label)
    {
        if (!ShouldDrawResultLabels(result))
            return false;
        if (label.empty())
            return false;
        return !ToolResultHasDuplicateTextLabel(result, label);
    }

    inline std::string BuildLabel(const ToolResult& result, const std::string& itemLabel)
    {
        return BuildToolResultOverlayLabel(result, itemLabel);
    }
}
