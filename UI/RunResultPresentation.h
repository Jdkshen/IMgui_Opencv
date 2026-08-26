#pragma once

#include "../Core/ToolInstance.h"
#include "../Algorithm/ToolResult.h"
#include "../include/imgui/imgui.h"

#include <string>

namespace UI::RunResultPresentation
{
    const char* StatusText(ToolResultStatus status);
    const char* StatusDescription(ToolResultStatus status);
    ImVec4 StatusColor(ToolResultStatus status, bool dark);
    ImVec4 StatusTextColor(ToolResultStatus status, bool dark);
    std::string ToolDisplayName(const ToolInstance& tool);
    std::string ResultSummary(const ToolResult& result);
    std::string ResultDetails(const ToolResult& result);
}
