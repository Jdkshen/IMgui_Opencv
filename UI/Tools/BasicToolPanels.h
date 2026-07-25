#pragma once

#include <functional>
#include <unordered_map>

#include "../ToolsWindow.h"

struct ToolPanelContext
{
    std::function<void(const char*)> beginCard;
    std::function<void()> endCard;
    std::function<void(const char*)> sectionHeader;
    std::function<bool(const char*)> primaryButton;
    std::function<bool(const char*)> secondaryButton;
    std::function<void(const char*, float)> parameterLabel;
    std::function<bool(int)> runTool;
    std::function<void(ToolInstance&, int)> drawSearchROI;
};

void RegisterBasicToolPanels(
    std::unordered_map<int, ToolUIFn>& panels,
    const ToolPanelContext& context);
