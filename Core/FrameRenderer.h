#pragma once

#include "DX12Context.h"

struct ImGuiIO;
struct ImVec4;

namespace FrameRenderer
{
    void RenderAndPresent(FrameContext* frameCtx, UINT backBufferIdx, const ImVec4& clearColor, const ImGuiIO& io);
}
