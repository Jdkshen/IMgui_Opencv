#pragma once

#include "ToolInstance.h"
#include "VisionContext.h"

// =====================================================
// ToolExecutor - unified tool execution entry point.
// Keep legacy global-state wrappers here while new tools move to VisionContext.
// =====================================================
namespace ToolExecutor
{
    bool RunViaITool(ToolInstance& it);
    bool RunViaITool(ToolInstance& it, VisionContext& ctx);

    bool Execute(int type, ToolInstance& it);
}
