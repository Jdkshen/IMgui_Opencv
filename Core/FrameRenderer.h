#pragma once

#include "DX12Context.h"

struct ImGuiIO;
struct ImVec4;

// =====================================================
// FrameRenderer — 每帧渲染管线封装
// 负责：资源屏障切换 → 清除渲染目标 → ImGui 绘制 → 呈现
// =====================================================
namespace FrameRenderer
{
    // 执行完整的一帧渲染：Present→RenderTarget 屏障、清除背景色、ImGui渲染、
    // RenderTarget→Present 屏障、执行命令列表、多视口平台窗口更新、Present
    void RenderAndPresent(FrameContext* frameCtx, UINT backBufferIdx, const ImVec4& clearColor, const ImGuiIO& io);
}
