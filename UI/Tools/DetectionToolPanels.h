#pragma once

#include "BasicToolPanels.h"

void RegisterDetectionToolPanels(
    std::unordered_map<int, ToolUIFn>& panels,
    const ToolPanelContext& context);
