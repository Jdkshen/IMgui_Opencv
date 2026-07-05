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
constexpr int APP_NUM_BACK_BUFFERS = 2;
constexpr int APP_SRV_HEAP_SIZE = 64;

// =========================
// 帧上下文：每个飞行帧关联一个命令分配器 + 围栏值
// 用于 GPU-CPU 同步：确保 GPU 完成上一帧渲染后，CPU 才能复用该帧的命令分配器
// =========================
struct FrameContext
{
    ID3D12CommandAllocator *CommandAllocator = nullptr; // 命令分配器：每帧独立的命令内存池
    UINT64 FenceValue = 0;                             // 围栏值：该帧提交时 GPU 需要达到的同步点
};

// =========================
// 简易空闲链表分配器：管理 SRV 描述符堆的分配/释放
// 使用栈式空闲链表（FreeIndices），O(1) 分配与释放
// 描述符索引 0 保留给主纹理，其余索引按需分配
// =========================
struct ExampleDescriptorHeapAllocator
{
    ID3D12DescriptorHeap *Heap = nullptr;                         // 管理的描述符堆
    D3D12_DESCRIPTOR_HEAP_TYPE HeapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
    D3D12_CPU_DESCRIPTOR_HANDLE HeapStartCpu = {};                // 堆起始 CPU 句柄
    D3D12_GPU_DESCRIPTOR_HANDLE HeapStartGpu = {};                // 堆起始 GPU 句柄
    UINT HeapHandleIncrement = 0;                                 // 单个描述符的句柄步长（字节）
    std::vector<int> FreeIndices;                                 // 空闲索引栈（尾部弹出=分配，尾部压入=释放）

    // 初始化分配器：绑定描述符堆，预填充空闲索引列表（索引0保留）
    void Create(ID3D12Device *device, ID3D12DescriptorHeap *heap)
    {
        assert(Heap == nullptr && FreeIndices.empty());
        Heap = heap;
        D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
        HeapType = desc.Type;
        HeapStartCpu = Heap->GetCPUDescriptorHandleForHeapStart();
        HeapStartGpu = Heap->GetGPUDescriptorHandleForHeapStart();
        HeapHandleIncrement = device->GetDescriptorHandleIncrementSize(HeapType);
        FreeIndices.reserve((int)desc.NumDescriptors);
        for (int n = desc.NumDescriptors; n > 1; n--)   // 索引0保留，从n-1到1入栈
            FreeIndices.push_back(n - 1);
    }

    // 销毁分配器：清空状态
    void Destroy()
    {
        Heap = nullptr;
        FreeIndices.clear();
    }

    // 分配一个描述符：从空闲栈顶弹出一个索引，计算对应的 CPU/GPU 句柄
    void Alloc(D3D12_CPU_DESCRIPTOR_HANDLE *out_cpu, D3D12_GPU_DESCRIPTOR_HANDLE *out_gpu)
    {
        assert(!FreeIndices.empty());
        int idx = FreeIndices.back();
        FreeIndices.pop_back();
        out_cpu->ptr = HeapStartCpu.ptr + (idx * HeapHandleIncrement);
        out_gpu->ptr = HeapStartGpu.ptr + (idx * HeapHandleIncrement);
    }

    // 释放一个描述符：将其索引压回空闲栈
    void Free(D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu)
    {
        int cpu_idx = (int)((cpu.ptr - HeapStartCpu.ptr) / HeapHandleIncrement);
        int gpu_idx = (int)((gpu.ptr - HeapStartGpu.ptr) / HeapHandleIncrement);
        assert(cpu_idx == gpu_idx); // CPU/GPU 句柄应指向同一描述符
        FreeIndices.push_back(cpu_idx);
    }
};

// =========================
// DX12 渲染状态全局变量（extern 声明）
// =========================
extern FrameContext g_frameContext[APP_NUM_FRAMES_IN_FLIGHT]; // 飞行帧上下文数组（双缓冲）
extern UINT g_frameIndex;                                      // 当前帧索引（自增，用于取模选择帧上下文）

extern ExampleDescriptorHeapAllocator g_pd3dSrvDescHeapAlloc;  // SRV 描述符堆分配器
extern ID3D12CommandQueue *g_pd3dCommandQueue;                 // 主命令队列
extern ID3D12Fence *g_fence;                                   // GPU 围栏（CPU-GPU 同步）
extern HANDLE g_fenceEvent;                                    // 围栏事件句柄
extern UINT64 g_fenceLastSignaledValue;                        // 最近一次 Signal 的围栏值
extern IDXGISwapChain3 *g_pSwapChain;                          // DX12 交换链（三重缓冲）
extern bool g_SwapChainTearingSupport;                         // 是否支持可变刷新率撕裂
extern bool g_SwapChainOccluded;                               // 交换链是否被遮挡
extern HANDLE g_hSwapChainWaitableObject;                      // 交换链等待对象（限制帧延迟）
extern ID3D12Resource *g_mainRenderTargetResource[APP_NUM_BACK_BUFFERS];       // 后备缓冲区资源
extern D3D12_CPU_DESCRIPTOR_HANDLE g_mainRenderTargetDescriptor[APP_NUM_BACK_BUFFERS]; // 后备缓冲区 RTV 描述符

// =========================
// 外部全局变量声明（OpenCV 纹理相关）
// =========================
extern ID3D12Device *gDevice;                  // 辅助 D3D12 设备（InitDX12Context 创建，用于纹理上传）
extern ID3D12GraphicsCommandList *gCmdList;    // 辅助命令列表（纹理拷贝/上传用）

extern ID3D12Device *g_pd3dDevice;              // 主 D3D12 设备（CreateDeviceD3D 创建，用于渲染）
extern ID3D12GraphicsCommandList *g_pd3dCommandList; // 主命令列表（每帧录制渲染命令）

extern ID3D12Resource *gTexture;                // OpenCV 纹理资源（CPU 可写、GPU 可读的上传堆）
extern D3D12_CPU_DESCRIPTOR_HANDLE srvHandle;   // 纹理 SRV 的 CPU 句柄
extern ID3D12DescriptorHeap *g_pd3dSrvDescHeap; // SRV 描述符堆（shader 可见）
extern ID3D12DescriptorHeap *g_pd3dRtvDescHeap; // RTV 描述符堆
extern ID3D12DescriptorHeap *g_pd3dDsvDescHeap; // DSV 描述符堆（预留）

extern D3D12_CPU_DESCRIPTOR_HANDLE gSrvCpuHandle; // SRV 堆起始 CPU 句柄（索引0 = 主纹理）
extern D3D12_GPU_DESCRIPTOR_HANDLE gSrvGpuHandle; // SRV 堆起始 GPU 句柄（ImGui::Image 使用）

extern std::string pendingPath;                 // 待加载图片路径（异步加载用）

extern int& gImageWidth;                        // 当前图片宽度引用（绑定到 ImageState）
extern int& gImageHeight;                       // 当前图片高度引用（绑定到 ImageState）

// =========================
// 函数声明
// =========================

// 初始化辅助 DX12 上下文（独立设备+命令列表，用于纹理上传等离线操作）
bool InitDX12Context();

// ===== DX12 设备管理 =====

// 创建主 D3D12 设备、交换链、命令队列/列表、围栏、描述符堆、渲染目标
bool CreateDeviceD3D(HWND hWnd);
// 清理所有 DX12 资源（交换链、命令列表、描述符堆、围栏、设备）
void CleanupDeviceD3D();
// 创建后备缓冲区的渲染目标视图
void CreateRenderTarget();
// 等待 GPU 完成所有操作后释放渲染目标
void CleanupRenderTarget();
// 等待所有待处理 GPU 操作完成（Signal + Wait 围栏）
void WaitForPendingOperations();
// 等待下一帧上下文可用（GPU 完成该帧渲染后返回对应的 FrameContext）
FrameContext *WaitForNextFrameContext();
