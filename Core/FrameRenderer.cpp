#include "FrameRenderer.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx12.h"

// =====================================================
// FrameRenderer::RenderAndPresent
// 每帧渲染管线的完整流程（按顺序）：
//   1. 资源屏障：Present → RenderTarget（GPU 可写）
//   2. 清除后备缓冲区为背景色
//   3. 绑定渲染目标 + 描述符堆
//   4. 提交 ImGui 绘制数据
//   5. 资源屏障：RenderTarget → Present（DXGI 可读）
//   6. 关闭命令列表 → 提交到命令队列执行
//   7. 多视口模式：更新/渲染额外平台窗口
//   8. 围栏信号 + Present 交换链
// =====================================================
namespace FrameRenderer
{
void RenderAndPresent(FrameContext* frameCtx, UINT backBufferIdx, const ImVec4& clearColor, const ImGuiIO& io)
{
    // ----- 1. 资源屏障：Present → RenderTarget -----
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = g_mainRenderTargetResource[backBufferIdx];
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;       // 从呈现状态
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;  // 转为渲染目标状态
    g_pd3dCommandList->Reset(frameCtx->CommandAllocator, nullptr);       // 重置命令列表（复用分配器）
    g_pd3dCommandList->ResourceBarrier(1, &barrier);

    // ----- 2. 清除渲染目标为背景色（预乘 Alpha） -----
    const float clearColorWithAlpha[4] = {
        clearColor.x * clearColor.w,
        clearColor.y * clearColor.w,
        clearColor.z * clearColor.w,
        clearColor.w};
    g_pd3dCommandList->ClearRenderTargetView(g_mainRenderTargetDescriptor[backBufferIdx], clearColorWithAlpha, 0, nullptr);

    // ----- 3. 绑定渲染目标 & 描述符堆 -----
    g_pd3dCommandList->OMSetRenderTargets(1, &g_mainRenderTargetDescriptor[backBufferIdx], FALSE, nullptr);
    g_pd3dCommandList->SetDescriptorHeaps(1, &g_pd3dSrvDescHeap);

    // ----- 4. 提交 ImGui 绘制数据到命令列表 -----
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_pd3dCommandList);

    // ----- 5. 资源屏障：RenderTarget → Present -----
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    g_pd3dCommandList->ResourceBarrier(1, &barrier);
    g_pd3dCommandList->Close();  // 关闭命令列表（录制完成）

    // ----- 6. 提交命令列表到 GPU 执行 -----
    g_pd3dCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&g_pd3dCommandList);

    // ----- 7. 多视口模式：更新额外平台窗口（拖拽到外部显示器） -----
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    // ----- 8. 围栏信号 → Present -----
    g_pd3dCommandQueue->Signal(g_fence, ++g_fenceLastSignaledValue);  // GPU 完成信号
    frameCtx->FenceValue = g_fenceLastSignaledValue;                  // 记录当前帧围栏值

    HRESULT hr = g_pSwapChain->Present(1, 0);  // vsync=1，无撕裂标志
    g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);  // 检测窗口遮挡
    g_frameIndex++;  // 推进帧索引
}
}
