#pragma once

#include "ToolInstance.h"
#include "VisionContext.h"

#include <vector>

// =====================================================
// ToolExecutor - unified tool execution entry point.
// Keep legacy global-state wrappers here while new tools move to VisionContext.
// =====================================================
namespace ToolExecutor
{
    struct ToolExecutionOutput
    {
        ToolResult result;
        float prepareMs = 0.0f;
        float executeMs = 0.0f;
        float publishMs = 0.0f;
        bool completed = false;
        bool cacheHit = false;
    };

    // Worker-safe algorithm phase. The caller owns immutable context and tool snapshots.
    bool ExecuteDetached(ToolInstance& toolSnapshot, VisionContext& context,
        int sourceToolIndex, ToolExecutionOutput& output);
    bool PrepareDetached(const ToolInstance& source, const cv::Mat& input,
        int sourceToolIndex, ToolInstance& toolSnapshot, VisionContext& context);
    // Worker-only task pipeline preparation. All dependencies and ROI/template
    // state are supplied as immutable snapshots; no global UI state is read.
    bool PrepareDetachedSnapshot(const ToolInstance& source,
        const cv::Mat& input, const cv::Mat& original, int sourceToolIndex,
        const std::vector<ToolInstance>& taskTools,
        const std::vector<int>& taskToolIndices,
        const std::vector<ROI>& visibleROIs, int selectedROI,
        const cv::Mat& frozenTemplate, ToolInstance& toolSnapshot,
        VisionContext& context);
    // UI-thread publication phase. Updates ImageState, history, overlays and runtime result.
    bool PublishDetached(ToolInstance& target, ToolExecutionOutput&& output);

    bool RunViaITool(ToolInstance& it);
    bool RunViaITool(ToolInstance& it, VisionContext& ctx);

    bool Execute(int type, ToolInstance& it);
}
