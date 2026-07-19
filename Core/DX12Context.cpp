#include "DX12Context.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <dxgi1_5.h>
#include "../Log/LogSystem.h"

// =========================
// DX12 渲染状态全局变量定义
// =========================
FrameContext g_frameContext[APP_NUM_FRAMES_IN_FLIGHT] = {};  // 飞行帧上下文（双缓冲）
UINT g_frameIndex = 0;                                        // 当前帧索引

ExampleDescriptorHeapAllocator g_pd3dSrvDescHeapAlloc;        // SRV 描述符堆分配器
ID3D12CommandQueue *g_pd3dCommandQueue = nullptr;              // 主命令队列
ID3D12Fence *g_fence = nullptr;                                // GPU 围栏
HANDLE g_fenceEvent = nullptr;                                 // 围栏事件句柄
UINT64 g_fenceLastSignaledValue = 0;                           // 最近一次 Signal 的围栏值
IDXGISwapChain3 *g_pSwapChain = nullptr;                       // 交换链
bool g_SwapChainTearingSupport = false;                        // 撕裂支持标志
bool g_SwapChainOccluded = false;                              // 遮挡标志
HANDLE g_hSwapChainWaitableObject = nullptr;                   // 交换链等待对象
ID3D12Resource *g_mainRenderTargetResource[APP_NUM_BACK_BUFFERS] = {};       // 后备缓冲区资源
D3D12_CPU_DESCRIPTOR_HANDLE g_mainRenderTargetDescriptor[APP_NUM_BACK_BUFFERS] = {}; // 后备缓冲区 RTV 描述符

// =========================
// OpenCV 纹理相关全局变量定义
// =========================
ID3D12Device *g_pd3dDevice = nullptr;                    // 主 D3D12 设备（渲染用）
ID3D12GraphicsCommandList *g_pd3dCommandList = nullptr;  // 主命令列表（渲染命令录制）
ID3D12Device *gDevice = nullptr;                         // 辅助 D3D12 设备（纹理上传用）
ID3D12GraphicsCommandList *gCmdList = nullptr;           // 辅助命令列表（纹理拷贝）
ID3D12Resource *gTexture = nullptr;                      // OpenCV 图片纹理资源
D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = {};              // 纹理 SRV 句柄
ID3D12DescriptorHeap *g_pd3dSrvDescHeap = nullptr;       // SRV 描述符堆
ID3D12DescriptorHeap *g_pd3dRtvDescHeap = nullptr;       // RTV 描述符堆

D3D12_CPU_DESCRIPTOR_HANDLE gSrvCpuHandle = {};          // SRV 堆起始 CPU 句柄（索引0）
D3D12_GPU_DESCRIPTOR_HANDLE gSrvGpuHandle = {};          // SRV 堆起始 GPU 句柄（索引0）

// =========================
// 初始化辅助 DX12 上下文
// 创建一个独立的 D3D12 设备 + 命令列表，专用于 OpenCV 图片纹理上传
// 与主渲染管线（CreateDeviceD3D）分开，避免资源竞争
// =========================
bool InitDX12Context()
{
    HRESULT hr;

    // 1. 创建 DXGI 工厂，枚举第一个非软件适配器
    IDXGIFactory4 *factory = nullptr;
    hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr))
        return false;

    IDXGIAdapter1 *adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++)
    {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)  // 跳过软件适配器（WARP）
        {
            adapter->Release();
            continue;
        }
        break;  // 使用第一个硬件适配器
    }

    // 2. 使用硬件适配器创建 D3D12 设备（功能级别 11.0）
    hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&gDevice));
    adapter->Release();
    factory->Release();
    if (FAILED(hr))
        return false;

    // 3. 创建命令队列 → 命令分配器 → 命令列表（Direct 类型）
    ID3D12CommandQueue *queue = nullptr;
    D3D12_COMMAND_QUEUE_DESC qDesc = {};
    qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    gDevice->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&queue));

    ID3D12CommandAllocator *allocator = nullptr;
    gDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));

    gDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&gCmdList));
    gCmdList->Close();  // 命令列表初始状态为关闭，使用时 Reset 即可

    // 4. 释放临时对象（命令列表已持有引用）
    allocator->Release();
    queue->Release();

    return true;
}

// =========================
// 等待下一帧上下文（GPU-CPU 同步）
// 1. 取当前帧对应的 FrameContext（通过 frameIndex % NUM_FRAMES_IN_FLIGHT）
// 2. 若 GPU 尚未完成该帧 → 等待围栏事件
// 3. 等待交换链 WaitableObject（限帧延迟，防止 CPU 超前 GPU 过多）
// =========================
FrameContext *WaitForNextFrameContext()
{
    FrameContext *frame_context = &g_frameContext[g_frameIndex % APP_NUM_FRAMES_IN_FLIGHT];

    // 检查 GPU 是否已完成该帧上下文对应的渲染
    if (g_fence->GetCompletedValue() < frame_context->FenceValue)
    {
        // 尚未完成：设置围栏事件，等待两个信号之一（交换链就绪或 GPU 完成）
        g_fence->SetEventOnCompletion(frame_context->FenceValue, g_fenceEvent);
        HANDLE waitableObjects[] = { g_hSwapChainWaitableObject, g_fenceEvent };
        ::WaitForMultipleObjects(2, waitableObjects, TRUE, INFINITE);
    }
    else
    {
        // GPU 已完成：仅等待交换链就绪
        ::WaitForSingleObject(g_hSwapChainWaitableObject, INFINITE);
    }

    return frame_context;
}

// =========================
// 创建 DX12 主渲染设备（完整管线初始化）
// 包含：交换链、命令队列/列表/分配器、围栏、RTV/SRV 描述符堆、渲染目标
// =========================
bool CreateDeviceD3D(HWND hWnd)
{
    // ----- 交换链描述 -----
    DXGI_SWAP_CHAIN_DESC1 sd;
    {
        ZeroMemory(&sd, sizeof(sd));
        sd.BufferCount       = APP_NUM_BACK_BUFFERS;                       // 后备缓冲区数量
        sd.Width             = 0;                                          // 0 = 自动匹配窗口大小
        sd.Height            = 0;
        sd.Format            = DXGI_FORMAT_R8G8B8A8_UNORM;                 // 标准 RGBA 格式
        sd.Flags             = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT; // 启用帧延迟控制
        sd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.SampleDesc.Count  = 1;                                          // 无多重采样
        sd.SampleDesc.Quality = 0;
        sd.SwapEffect        = DXGI_SWAP_EFFECT_FLIP_DISCARD;              // Flip 模型（现代 DX12 标准）
        sd.AlphaMode         = DXGI_ALPHA_MODE_UNSPECIFIED;
        sd.Scaling           = DXGI_SCALING_STRETCH;
        sd.Stereo            = FALSE;
    }

    // ----- 调试层（仅 Debug 构建启用） -----
#ifdef DX12_ENABLE_DEBUG_LAYER
    ID3D12Debug* pdx12Debug = nullptr;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pdx12Debug))))
        pdx12Debug->EnableDebugLayer();
#endif

    // ----- 创建 D3D12 设备（功能级别 11.0） -----
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    if (D3D12CreateDevice(nullptr, featureLevel, IID_PPV_ARGS(&g_pd3dDevice)) != S_OK)
        return false;

    // ----- 配置调试信息队列（过滤已知无害警告） -----
#ifdef DX12_ENABLE_DEBUG_LAYER
    if (pdx12Debug != nullptr)
    {
        ID3D12InfoQueue* pInfoQueue = nullptr;
        g_pd3dDevice->QueryInterface(IID_PPV_ARGS(&pInfoQueue));
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

        // 过滤 Fence 零值等待警告（首次 Signal 前的正常行为）
        const int D3D12_MESSAGE_ID_FENCE_ZERO_WAIT_ = 1424;
        D3D12_MESSAGE_ID disabledMessages[] = { (D3D12_MESSAGE_ID)D3D12_MESSAGE_ID_FENCE_ZERO_WAIT_ };
        D3D12_INFO_QUEUE_FILTER filter = {};
        filter.DenyList.NumIDs  = 1;
        filter.DenyList.pIDList = disabledMessages;
        pInfoQueue->AddStorageFilterEntries(&filter);
        pInfoQueue->Release();
        pdx12Debug->Release();
    }
#endif

    // ----- 创建 RTV 描述符堆（每个后备缓冲区一个 RTV） -----
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = APP_NUM_BACK_BUFFERS;
        desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        desc.NodeMask       = 1;
        if (g_pd3dDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dRtvDescHeap)) != S_OK)
            return false;

        // 预计算每个后备缓冲区的 RTV 句柄偏移
        SIZE_T rtvDescriptorSize = g_pd3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_pd3dRtvDescHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < APP_NUM_BACK_BUFFERS; i++)
        {
            g_mainRenderTargetDescriptor[i] = rtvHandle;
            rtvHandle.ptr += rtvDescriptorSize;
        }
    }

    // ----- 创建 SRV 描述符堆（Shader 可见，用于 ImGui 纹理绑定） -----
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = APP_SRV_HEAP_SIZE;
        desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;  // GPU 可读
        if (g_pd3dDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dSrvDescHeap)) != S_OK)
            return false;
        g_pd3dSrvDescHeapAlloc.Create(g_pd3dDevice, g_pd3dSrvDescHeap);
    }

    // ----- 创建命令队列（Direct 类型，支持所有图形/计算/拷贝命令） -----
    {
        D3D12_COMMAND_QUEUE_DESC desc = {};
        desc.Type     = D3D12_COMMAND_LIST_TYPE_DIRECT;
        desc.Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE;
        desc.NodeMask = 1;
        if (g_pd3dDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(&g_pd3dCommandQueue)) != S_OK)
            return false;
    }

    // ----- 创建命令分配器（每个飞行帧一个，允许 GPU 并行处理多帧） -----
    for (UINT i = 0; i < APP_NUM_FRAMES_IN_FLIGHT; i++)
        if (g_pd3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_frameContext[i].CommandAllocator)) != S_OK)
            return false;

    // ----- 创建命令列表（绑定到第0帧的分配器，初始状态为关闭） -----
    if (g_pd3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frameContext[0].CommandAllocator, nullptr, IID_PPV_ARGS(&g_pd3dCommandList)) != S_OK ||
        g_pd3dCommandList->Close() != S_OK)
        return false;

    // ----- 创建围栏 + 事件（GPU-CPU 同步基础设施） -----
    if (g_pd3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)) != S_OK)
        return false;

    g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);  // 自动重置事件
    if (g_fenceEvent == nullptr)
        return false;

    // ----- 创建交换链（关联窗口句柄和命令队列） -----
    {
        IDXGIFactory5 *dxgiFactory = nullptr;
        IDXGISwapChain1 *swapChain1 = nullptr;
        if (CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)) != S_OK)
            return false;

        // 检测硬件是否支持可变刷新率撕裂（Tearing）
        BOOL allow_tearing = FALSE;
        dxgiFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow_tearing, sizeof(allow_tearing));
        g_SwapChainTearingSupport = (allow_tearing == TRUE);
        if (g_SwapChainTearingSupport)
            sd.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

        // 为指定窗口创建交换链
        if (dxgiFactory->CreateSwapChainForHwnd(g_pd3dCommandQueue, hWnd, &sd, nullptr, nullptr, &swapChain1) != S_OK)
            return false;
        if (swapChain1->QueryInterface(IID_PPV_ARGS(&g_pSwapChain)) != S_OK)
            return false;

        // 禁用 Alt+Enter 全屏切换（ImGui 多视口模式下避免冲突）
        if (g_SwapChainTearingSupport)
            dxgiFactory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);

        swapChain1->Release();
        dxgiFactory->Release();

        // 设置最大帧延迟 = 后备缓冲区数量（控制 CPU 超前 GPU 的程度）
        g_pSwapChain->SetMaximumFrameLatency(APP_NUM_BACK_BUFFERS);
        g_hSwapChainWaitableObject = g_pSwapChain->GetFrameLatencyWaitableObject();
    }

    // ----- 创建渲染目标视图（从交换链获取后备缓冲区） -----
    CreateRenderTarget();
    return true;
}

// =========================
// 清理所有 DX12 资源
// 按依赖顺序逆序释放：渲染目标 → 交换链 → 命令分配器 → 命令队列/列表 → 描述符堆 → 围栏 → 设备
// Debug 构建下通过 DXGI 调试接口报告未释放的活跃对象
// =========================
void CleanupDeviceD3D()
{
    // 1. 先释放渲染目标（依赖交换链的后备缓冲区）
    CleanupRenderTarget();

    // 2. 退出全屏 → 释放交换链
    if (g_pSwapChain)
    {
        g_pSwapChain->SetFullscreenState(false, nullptr);
        g_pSwapChain->Release();
        g_pSwapChain = nullptr;
    }
    if (g_hSwapChainWaitableObject != nullptr)
        CloseHandle(g_hSwapChainWaitableObject);

    // 3. 释放飞行帧的命令分配器
    for (UINT i = 0; i < APP_NUM_FRAMES_IN_FLIGHT; i++)
        if (g_frameContext[i].CommandAllocator)
        {
            g_frameContext[i].CommandAllocator->Release();
            g_frameContext[i].CommandAllocator = nullptr;
        }

    // 4. 释放命令队列和命令列表
    if (g_pd3dCommandQueue)  { g_pd3dCommandQueue->Release(); g_pd3dCommandQueue = nullptr; }
    if (g_pd3dCommandList)   { g_pd3dCommandList->Release();  g_pd3dCommandList  = nullptr; }

    // 5. 释放描述符堆
    if (g_pd3dRtvDescHeap)   { g_pd3dRtvDescHeap->Release();  g_pd3dRtvDescHeap   = nullptr; }
    if (g_pd3dSrvDescHeap)   { g_pd3dSrvDescHeap->Release();  g_pd3dSrvDescHeap   = nullptr; }

    // 6. 释放围栏和事件
    if (g_fence)             { g_fence->Release();             g_fence             = nullptr; }
    if (g_fenceEvent)        { CloseHandle(g_fenceEvent);      g_fenceEvent        = nullptr; }

    // 7. 最后释放设备
    if (g_pd3dDevice)        { g_pd3dDevice->Release();        g_pd3dDevice        = nullptr; }

    // Debug 构建：报告未释放的 DXGI 对象（帮助定位资源泄漏）
#ifdef DX12_ENABLE_DEBUG_LAYER
    IDXGIDebug1* pDebug = nullptr;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pDebug))))
    {
        pDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_SUMMARY);
        pDebug->Release();
    }
#endif
}

// =========================
// 创建渲染目标视图（RTV）
// 从交换链获取后备缓冲区 → 为每个缓冲区创建 RTV，绑定到预分配的描述符句柄
// =========================
void CreateRenderTarget()
{
    for (UINT i = 0; i < APP_NUM_BACK_BUFFERS; i++)
    {
        ID3D12Resource* pBackBuffer = nullptr;
        g_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));           // 获取后备缓冲区资源
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, g_mainRenderTargetDescriptor[i]); // 创建 RTV
        g_mainRenderTargetResource[i] = pBackBuffer;                       // 保存资源指针（后续释放用）
    }
}

// =========================
// 清理渲染目标资源
// 先等待 GPU 完成所有待处理操作 → 再释放所有后备缓冲区
// =========================
void CleanupRenderTarget()
{
    WaitForPendingOperations();  // 确保 GPU 不再引用这些资源
    for (UINT i = 0; i < APP_NUM_BACK_BUFFERS; i++)
        if (g_mainRenderTargetResource[i])
        {
            g_mainRenderTargetResource[i]->Release();
            g_mainRenderTargetResource[i] = nullptr;
        }
}

// =========================
// 等待所有待处理 GPU 操作完成
// 工作原理：Signal 围栏（递增围栏值） → 等待 GPU 到达该围栏值
// 调用后保证：此前提交的所有命令都已被 GPU 执行完毕
// =========================
void WaitForPendingOperations()
{
    g_pd3dCommandQueue->Signal(g_fence, ++g_fenceLastSignaledValue);  // 在队列中插入围栏信号
    g_fence->SetEventOnCompletion(g_fenceLastSignaledValue, g_fenceEvent); // 围栏到达时触发事件
    ::WaitForSingleObject(g_fenceEvent, INFINITE);                    // 阻塞等待 GPU 完成
}
