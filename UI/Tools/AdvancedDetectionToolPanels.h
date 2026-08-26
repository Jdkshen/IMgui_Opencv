#pragma once

#include "BasicToolPanels.h"

void RegisterAdvancedDetectionToolPanels(
    std::unordered_map<int, ToolUIFn>& panels,
    const ToolPanelContext& context);
