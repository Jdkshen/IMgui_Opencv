#pragma once

#include "BasicToolPanels.h"

void RegisterMeasurementToolPanel(
    std::unordered_map<int, ToolUIFn>& panels,
    const ToolPanelContext& context);
