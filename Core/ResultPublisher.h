#pragma once
#include "../Core/VisionContext.h"
#include "../Core/RealtimeDetectionState.h"

#include <utility>

// =====================================================
// ResultPublisher — 统一结果清理
// =====================================================
inline void ClearAllResults()
{
    gContext.ClearUnifiedResults();
    RealtimeDetectionState::Clear();
}

inline void PublishUnifiedResult(ToolResult result)
{
    ClearAllResults();
    gContext.unifiedResults.push_back(std::move(result));
}
