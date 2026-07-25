#pragma once

#include "../Core/GeometryPrimitive.h"

struct ImDrawList;
struct ToolInstance;

namespace UI::GeometryDrawEditor
{
void Cancel();
bool DrawToolPanel(ToolInstance& tool, int toolIndex);
void DrawCanvasOverlay(ImDrawList* drawList);
bool IsCanvasActive();
bool HandleCanvasInteraction();
bool ConsumeChanged();
}
