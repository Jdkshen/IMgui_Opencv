#pragma once

#include "RunResultSnapshot.h"
#include "../include/imgui/imgui.h"

namespace UI::RunResultOverlayRenderer
{
void DrawResultImageOverlays(
    const RunResultSnapshotModel::RunResultSnapshot& snapshot,
    const ImVec2& imageMin,
    const ImVec2& imageMax,
    float scale,
    bool allowLabels);
}
