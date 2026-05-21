// Windows_imgui.cpp : 应用程序入口点 - DirectX12 + Dear ImGui + OpenCV 桌面视觉工具中文版
#include "framework.h"
#include "Windows_imgui.h"
#include <fstream>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

// =========================
// 全局变量定义（只能这里写一次）
// =========================

static std::string uploadRequest;                // 图片加载请求路径（主循环处理）
static bool requestLoadImage = false;            // 图片加载请求标志
HWND g_hWnd = nullptr;                    // 主窗口句柄（主题切换用）
static float g_DPIScale = 1.0f;                   // DPI 缩放（主题切换时复用）

// 全局或主循环前定义
static ImVec4 clear_color = ImVec4(
    0.45f,
    0.55f,
    0.60f,
    1.00f
);

// ==========================
// 主题切换（0=Dark 1=Light 2=Classic）
// ==========================
int g_CurrentTheme = 0;
const char* g_ThemeNames[] = { "夜间", "白天" };

void ApplyTheme(int theme)
{
    if (theme == 0)
        ImGui::StyleColorsDark();
    else
        ImGui::StyleColorsLight();
    g_CurrentTheme = theme;

    ImGuiStyle& style = ImGui::GetStyle();
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    if (g_hWnd)
    {
        BOOL dark = (theme == 0);
        DwmSetWindowAttribute(g_hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    }

    // 写入文件持久化
    std::ofstream f("theme.cfg", std::ios::trunc);
    if (f) f << theme;
}

void LoadTheme()
{
    std::ifstream f("theme.cfg");
    int t = 0;
    if (f >> t) ApplyTheme(t);
}


// 前向声明
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// =========================
// 程序入口点
// =========================
int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR lpCmdLine,
	_In_ int nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	// ===== 阶段1：DPI感知设置 =====
	// 使进程支持DPI感知，获取主监视器的缩放比例
	ImGui_ImplWin32_EnableDpiAwareness();
	float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));
	g_DPIScale = main_scale;

	// ===== 阶段2：创建应用程序窗口 =====
	WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui Example", nullptr };
	::RegisterClassExW(&wc);
	HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Dear ImGui DirectX12", WS_OVERLAPPEDWINDOW, 100, 20, (int)(1280 * main_scale), (int)(800 * main_scale), nullptr, nullptr, wc.hInstance, nullptr);
	g_hWnd = hwnd;

	// ===== 阶段3：初始化 Direct3D 12 =====
	if (!CreateDeviceD3D(hwnd))
	{
		CleanupDeviceD3D();
		::UnregisterClassW(wc.lpszClassName, wc.hInstance);
		return 1;
	}
	// 初始化辅助 DX12 上下文（用于 OpenCV 图片纹理上传）
	InitDX12Context();

	// 显示窗口
	::ShowWindow(hwnd, SW_SHOWDEFAULT);
	::UpdateWindow(hwnd);

	// ===== 阶段4：初始化 Dear ImGui =====
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	(void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // 启用键盘导航
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // 启用手柄导航
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;      // 启用停靠系统
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;    // 启用多视口支持

	// 设置 ImGui 风格
	LoadTheme();  // 加载上次保存的主题

	// 设置 DPI 缩放
	ImGuiStyle& style = ImGui::GetStyle();
	style.ScaleAllSizes(main_scale);   // 烘焙固定风格缩放
	style.FontScaleDpi = main_scale;   // 字体DPI缩放
	io.ConfigDpiScaleFonts = true;     // 实验性：自动缩放字体
	io.ConfigDpiScaleViewports = true; // 实验性：DPI变化时缩放视口

	// 多视口模式下：窗口圆角归零，背景不透明度设为1
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
	}

	// 设置平台/渲染后端
	ImGui_ImplWin32_Init(hwnd);

	ImGui_ImplDX12_InitInfo init_info = {};
	init_info.Device = g_pd3dDevice;
	init_info.CommandQueue = g_pd3dCommandQueue;
	init_info.NumFramesInFlight = APP_NUM_FRAMES_IN_FLIGHT;
	init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
	// 应用程序提供SRV描述符分配函数（ImGui不直接管理纹理描述符）
	init_info.SrvDescriptorHeap = g_pd3dSrvDescHeap;
	init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle)
		{ return g_pd3dSrvDescHeapAlloc.Alloc(out_cpu_handle, out_gpu_handle); };
	init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle)
		{ return g_pd3dSrvDescHeapAlloc.Free(cpu_handle, gpu_handle); };

	ImGui_ImplDX12_Init(&init_info);

	// 获取SRV堆的起始句柄（用于纹理创建和 ImGui::Image 显示）
	gSrvCpuHandle = g_pd3dSrvDescHeap->GetCPUDescriptorHandleForHeapStart();
	gSrvGpuHandle = g_pd3dSrvDescHeap->GetGPUDescriptorHandleForHeapStart();

	// ===== 阶段5：加载字体（支持中文显示）=====
	FontManager::InitFonts(main_scale);

	// ===== 状态变量 =====
	bool done = false;

	// ===== 阶段6：主循环 =====
	while (!done)
	{
		// ----- 6.1 Windows消息处理 -----
		MSG msg;
		while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
		{
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
			if (msg.message == WM_QUIT)
				done = true;
		}
		if (done)
			break;

		// ----- 6.2 窗口遮挡/最小化处理 -----
		// 如果窗口被遮挡或最小化，让出CPU时间
		if ((g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) || ::IsIconic(hwnd))
		{
			::Sleep(10);
			continue;
		}
		g_SwapChainOccluded = false;

		// ----- 6.3 开启新的ImGui帧 -----
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		// =========================
		// UI 绘制
		// =========================
		UI::DrawDockSpaceHost();     // 主停靠空间 + 菜单栏
		UI::ShowSidebar();           // 侧边栏控制面板
		UI::ShowLogWindow();         // 日志窗口
		UI::ShowStatsWindow();       // 性能统计窗口
		UI::ShowOpenCV();            // 图片显示窗口
		UI::ShowToolsWindow();       // 工具窗口（ROI管理+算法入口）
		ThresholdTool::ShowThresholdWindow();  // 阈值调试/图像处理窗口
		TemplateMatch::ShowWindow();           // 模板匹配调试窗口
		TemplateMatch::ShowTemplateEditor();   // 模板编辑弹窗

		// ----- 6.4 渲染 Dear ImGui 绘制数据 -----
		ImGui::Render();

		// ----- 6.5 准备 DX12 渲染上下文 -----
		FrameContext* frameCtx = WaitForNextFrameContext();
		UINT backBufferIdx = g_pSwapChain->GetCurrentBackBufferIndex();

		frameCtx->CommandAllocator->Reset();
		g_pd3dCommandList->Reset(frameCtx->CommandAllocator, nullptr);

		// =========================
		// ⭐ 处理图片加载请求（UI线程写入→主循环读取）
		// =========================
		if (!pendingPath.empty())
		{
			uploadRequest = pendingPath;
			pendingPath.clear();
			requestLoadImage = true;
		}

		// =========================
		// ⭐ 执行图片加载（同步方式，后续可改为异步）
		// =========================
		if (requestLoadImage && !uploadRequest.empty())
		{
			// 释放上一张图片的GPU纹理（GPU已完成渲染）
			FlushPendingRelease();

			LogSystem::Add(LOG_INFO, "开始加载图片...");

			static OpenCVTest test;
			test.TestReadImage(
				uploadRequest,
				g_pd3dDevice,
				g_pd3dCommandList,
				&gTexture,
				gSrvCpuHandle
			);

			LogSystem::Add(LOG_INFO, "图片加载完成");
			UI::FitImageToWindow();  // 自动调整缩放使图片适配窗口

			//uploadRequest.clear();
			requestLoadImage = false;
		}

		// =========================
		// ⭐ GPU上传（阈值处理/图像管线产出的结果上传到显存）
		// =========================
		if (gNeedUpload)
		{
			LogSystem::Add(LOG_INFO, color, "UPLOAD_BEGIN");

			UploadToDX12(
				g_pd3dDevice,
				g_pd3dCommandList,
				&gTexture,
				gPendingUpload,
				DXGI_FORMAT_R8G8B8A8_UNORM,
				gSrvCpuHandle
			);
			gNeedUpload = false;
			LogSystem::Add(LOG_INFO, color, "UPLOAD_END");
		}

		// ----- 6.6 渲染管线：状态切换 + 绘制 + 呈现 -----
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = g_mainRenderTargetResource[backBufferIdx];
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		g_pd3dCommandList->Reset(frameCtx->CommandAllocator, nullptr);
		g_pd3dCommandList->ResourceBarrier(1, &barrier);

		// 清屏渲染目标
		const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
		g_pd3dCommandList->ClearRenderTargetView(g_mainRenderTargetDescriptor[backBufferIdx], clear_color_with_alpha, 0, nullptr);
		g_pd3dCommandList->OMSetRenderTargets(1, &g_mainRenderTargetDescriptor[backBufferIdx], FALSE, nullptr);
		g_pd3dCommandList->SetDescriptorHeaps(1, &g_pd3dSrvDescHeap);
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_pd3dCommandList);

		// 切换回呈现状态
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		g_pd3dCommandList->ResourceBarrier(1, &barrier);
		g_pd3dCommandList->Close();

		// 提交命令列表
		g_pd3dCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&g_pd3dCommandList);

		// 更新和渲染附加的平台窗口（多视口支持）
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		}

		// GPU围栏信号同步
		g_pd3dCommandQueue->Signal(g_fence, ++g_fenceLastSignaledValue);
		frameCtx->FenceValue = g_fenceLastSignaledValue;

		// 呈现画面到屏幕
		HRESULT hr = g_pSwapChain->Present(1, 0); // 带垂直同步
		g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
		g_frameIndex++;
	}

	// ===== 清理阶段：等待GPU完成，释放所有资源 =====
	WaitForPendingOperations();
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	CleanupDeviceD3D();
	::DestroyWindow(hwnd);
	::UnregisterClassW(wc.lpszClassName, wc.hInstance);

	return 0;
}

// 前向声明 Win32 消息处理器（来自 imgui_impl_win32.cpp）
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// =========================
// Win32 窗口消息处理函数
// =========================
// 重要提示：
// - io.WantCaptureMouse 为 true 时，不要将鼠标输入传递给应用程序
// - io.WantCaptureKeyboard 为 true 时，不要将键盘输入传递给应用程序
// 通常将所有输入先传递给 Dear ImGui，再根据标志决定是否拦截
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	switch (msg)
	{
case WM_SIZE:
		// 窗口大小变化时重建渲染目标（最小化时不处理）
		if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED)
		{
			CleanupRenderTarget();
			DXGI_SWAP_CHAIN_DESC1 desc = {};
			g_pSwapChain->GetDesc1(&desc);
			HRESULT result = g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), desc.Format, desc.Flags);
			IM_ASSERT(SUCCEEDED(result) && "调整交换链大小失败");
			CreateRenderTarget();
		}
		return 0;
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU) // 禁用ALT键激活菜单
			return 0;
		break;
	case WM_DESTROY:
		::PostQuitMessage(0);  // 发送退出消息
		return 0;
	}
	return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}