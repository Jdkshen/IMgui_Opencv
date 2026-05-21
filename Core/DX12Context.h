#pragma once
#include <d3d12.h>
#include <string>
#include <vector>
#include <cassert>
#include <dxgi1_4.h>

// =========================
// ImGui DX12 渲染配置常量
// =========================
constexpr int APP_NUM_FRAMES_IN_FLIGHT = 2;
constexpr int APP_NUM_BACK_BUFFERS     = 2;
constexpr int APP_SRV_HEAP_SIZE        = 64;

// =========================
// 帧上下文：每个飞行帧关联一个命令分配器 + 围栏值
// =========================
struct FrameContext
{
    ID3D12CommandAllocator* CommandAllocator = nullptr;
    UINT64                  FenceValue       = 0;
};

// =========================
// 简易空闲链表分配器：管理 SRV 描述符堆的分配/释放
// =========================
struct ExampleDescriptorHeapAllocator
{
    ID3D12DescriptorHeap*        Heap                 = nullptr;
    D3D12_DESCRIPTOR_HEAP_TYPE   HeapType             = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
    D3D12_CPU_DESCRIPTOR_HANDLE  HeapStartCpu         = {};
    D3D12_GPU_DESCRIPTOR_HANDLE  HeapStartGpu         = {};
    UINT                         HeapHandleIncrement  = 0;
    std::vector<int>             FreeIndices;

    void Create(ID3D12Device* device, ID3D12DescriptorHeap* heap)
    {
        assert(Heap == nullptr && FreeIndices.empty());
        Heap = heap;
        D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
        HeapType = desc.Type;
        HeapStartCpu = Heap->GetCPUDescriptorHandleForHeapStart();
        HeapStartGpu = Heap->GetGPUDescriptorHandleForHeapStart();
        HeapHandleIncrement = device->GetDescriptorHandleIncrementSize(HeapType);
        FreeIndices.reserve((int)desc.NumDescriptors);
        for (int n = desc.NumDescriptors; n > 1; n--)
            FreeIndices.push_back(n - 1);
    }

    void Destroy()
    {
        Heap = nullptr;
        FreeIndices.clear();
    }

    void Alloc(D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu)
    {
        assert(!FreeIndices.empty());
        int idx = FreeIndices.back();
        FreeIndices.pop_back();
        out_cpu->ptr = HeapStartCpu.ptr + (idx * HeapHandleIncrement);
        out_gpu->ptr = HeapStartGpu.ptr + (idx * HeapHandleIncrement);
    }

    void Free(D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu)
    {
        int cpu_idx = (int)((cpu.ptr - HeapStartCpu.ptr) / HeapHandleIncrement);
        int gpu_idx = (int)((gpu.ptr - HeapStartGpu.ptr) / HeapHandleIncrement);
        assert(cpu_idx == gpu_idx);
        FreeIndices.push_back(cpu_idx);
    }
};

// =========================
// DX12 渲染状态全局变量（extern 声明）
// =========================
extern FrameContext                 g_frameContext[APP_NUM_FRAMES_IN_FLIGHT];
extern UINT                         g_frameIndex;

extern ExampleDescriptorHeapAllocator g_pd3dSrvDescHeapAlloc;
extern ID3D12CommandQueue*          g_pd3dCommandQueue;
extern ID3D12Fence*                 g_fence;
extern HANDLE                       g_fenceEvent;
extern UINT64                       g_fenceLastSignaledValue;
extern IDXGISwapChain3*             g_pSwapChain;
extern bool                         g_SwapChainTearingSupport;
extern bool                         g_SwapChainOccluded;
extern HANDLE                       g_hSwapChainWaitableObject;
extern ID3D12Resource*              g_mainRenderTargetResource[APP_NUM_BACK_BUFFERS];
extern D3D12_CPU_DESCRIPTOR_HANDLE  g_mainRenderTargetDescriptor[APP_NUM_BACK_BUFFERS];

// =========================
// 外部全局变量声明（OpenCV 纹理相关）
// =========================
extern ID3D12Device* gDevice;
extern ID3D12GraphicsCommandList* gCmdList;

extern ID3D12Device* g_pd3dDevice;
extern ID3D12GraphicsCommandList* g_pd3dCommandList;

extern ID3D12Resource* gTexture;
extern D3D12_CPU_DESCRIPTOR_HANDLE srvHandle;
extern ID3D12DescriptorHeap* g_pd3dSrvDescHeap;
extern ID3D12DescriptorHeap* g_pd3dRtvDescHeap;

extern D3D12_CPU_DESCRIPTOR_HANDLE gSrvCpuHandle;
extern D3D12_GPU_DESCRIPTOR_HANDLE gSrvGpuHandle;

extern std::string pendingPath;

extern int gImageWidth;
extern int gImageHeight;

// =========================
// 函数声明
// =========================
bool InitDX12Context();

// DX12 设备管理
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
void WaitForPendingOperations();
FrameContext* WaitForNextFrameContext();

