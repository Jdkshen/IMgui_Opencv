// ============================================================================
// Windows_imgui.cpp : 应用程序入口点
// 技术栈：DirectX12（优先）+ DirectX11（回退） + Dear ImGui + OpenCV 5.0
// 功能：桌面机器视觉工具 — 支持工业相机、YOLO检测、模板匹配、OCR等
// 渲染管线：ImGui 绘制 UI → GPU 上传纹理 → DX12/DX11 交换链呈现
// ============================================================================
#include "framework.h"      // Windows 基础头文件（Win32 API、COM 等）
#include "Windows_imgui.h"   // 项目主头文件：全局变量声明、ImGui/OpenCV 头文件聚合
#include "resource.h"        // VS 资源文件：图标 ID、版本信息等
#include "Core/ImageLoadController.h"   // 异步图片加载调度器：请求加载 → 后台解码 → 回调通知
#include "Core/ImageState.h"            // 当前图像的全局状态：原始图、处理结果、脏标记
#include "Core/AppRuntimeState.h"       // 应用运行时状态：窗口句柄、DPI缩放、全局配置
#include "Core/LiveYoloRunner.h"        // YOLO 实时检测运行器：摄像头帧 → ONNX推理 → 检测框叠加
#include "Core/GraphicsBackend.h"       // 图形后端抽象层：自动选择 DX12/DX11，封装初始化/渲染/呈现
#include "Core/VisionContext.h"         // 视觉处理上下文：携带当前图像、ROI、标定参数给所有算法工具
#include "Core/RecipeAutosaveService.h" // 配方自动保存服务：定时备份当前工具链配置到磁盘
#include "Core/ToolController.h"        // 工具控制器：管理算法工具链的生命周期（创建/执行/销毁）
#include "Renderer/PreviewTextureCache.h" // 预览纹理缓存：小图预览结果缓存为GPU纹理，避免重复上传
#include "UI/HardwareWindow.h"          // 硬件连接窗口：设备发现、连接管理、PLC通信状态
#include "UI/DockSpaceHost.h"           // 主停靠空间宿主：菜单栏 + 中央DockSpace容器
#include "UI/RunResultWindow.h"         // 运行结果看板：整链执行完成后展示汇总结果
#include <DbgHelp.h>
#include <filesystem>
#include <fstream>
#include <cstdlib>
// ---- 静态链接库（链接时自动解析，无需 LoadLibrary）----
#pragma comment(lib, "dwmapi.lib")       // 桌面窗口管理器：DwmSetWindowAttribute（圆角窗口）
#pragma comment(lib, "d3d11.lib")         // DirectX 11 核心库：设备创建、渲染管线
#pragma comment(lib, "d3d12.lib")         // DirectX 12 核心库：命令队列、描述符堆、资源屏障
#pragma comment(lib, "dxgi.lib")          // DXGI 交换链：swap chain 创建、Present、全屏切换
#pragma comment(lib, "dxguid.lib")        // DirectX GUID 定义：IID_ID3D12Device 等接口标识符
#pragma comment(lib, "d3dcompiler.lib")   // HLSL 着色器编译器：运行时编译 .hlsl → DXBC/DXIL
#pragma comment(lib, "Comdlg32.lib")      // 通用对话框：文件打开/保存对话框（选择图片/模型）
#pragma comment(lib, "Dbghelp.lib")
// ---- OpenCV 库：Debug/Release 自动切换 ----
#ifdef _DEBUG
#pragma comment(lib, "opencv_world500d.lib")  // OpenCV 5.0 Debug（含 cv::imread/imshow/滤波等全部模块）
#else
#pragma comment(lib, "opencv_world500.lib")   // OpenCV 5.0 Release
#endif

// =========================
// 全局变量定义（只能在此 .cpp 中定义一次，.h 中用 extern 声明）
// =========================

// 渲染背景色：ImGui 窗口之外的清屏颜色（灰蓝色调，RGB 0.45/0.55/0.60）
// 在主循环每帧渲染前用于 ClearRenderTarget
static ImVec4 clear_color = ImVec4(
	0.45f,  // R：红色分量
	0.55f,  // G：绿色分量
	0.60f,  // B：蓝色分量
	1.00f); // A：完全不透明

// 前向声明：窗口消息处理函数（定义在本文件末尾）
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
std::filesystem::path StartupDataDirectory()
{
    wchar_t* localAppData = nullptr;
    std::size_t length = 0;
    std::filesystem::path directory = std::filesystem::current_path();
    if (_wdupenv_s(&localAppData, &length, L"LOCALAPPDATA") == 0 &&
        localAppData && length > 1)
    {
        directory = std::filesystem::path(localAppData) / L"IMgui_Opencv";
    }
    std::free(localAppData);
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    return directory;
}

void AppendStartupLog(const std::string& message)
{
    std::ofstream output(StartupDataDirectory() / L"startup.log",
        std::ios::app | std::ios::binary);
    SYSTEMTIME time{};
    GetLocalTime(&time);
    output << time.wYear << '-' << time.wMonth << '-' << time.wDay << ' '
           << time.wHour << ':' << time.wMinute << ':' << time.wSecond
           << " | " << message << '\n';
}

std::string Win32ErrorMessage(const char* operation, DWORD error)
{
    char* systemMessage = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0,
        reinterpret_cast<char*>(&systemMessage), 0, nullptr);
    std::string result = operation + std::string(" failed (Win32=") +
        std::to_string(error) + ")";
    if (systemMessage)
    {
        result += ": ";
        result += systemMessage;
        LocalFree(systemMessage);
    }
    return result;
}

void ShowStartupError(const std::string& message)
{
    AppendStartupLog("ERROR | " + message);
    MessageBoxA(nullptr, message.c_str(), "IMgui_Opencv startup error",
        MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

LONG WINAPI WriteCrashDump(EXCEPTION_POINTERS* exceptionPointers)
{
    const std::filesystem::path directory = StartupDataDirectory();
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t fileName[96]{};
    swprintf_s(fileName, L"crash_%04u%02u%02u_%02u%02u%02u.dmp",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
    const std::filesystem::path dumpPath = directory / fileName;
    HANDLE file = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION info{};
        info.ThreadId = GetCurrentThreadId();
        info.ExceptionPointers = exceptionPointers;
        info.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
            MiniDumpNormal, exceptionPointers ? &info : nullptr, nullptr, nullptr);
        CloseHandle(file);
    }
    AppendStartupLog("FATAL | unhandled exception; dump=" + dumpPath.string());
    MessageBoxW(nullptr,
        L"程序发生未处理异常，崩溃转储已写入 LOCALAPPDATA\\IMgui_Opencv。",
        L"IMgui_Opencv", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    return EXCEPTION_EXECUTE_HANDLER;
}
}

// =========================
// 程序入口点 — Win32 GUI 应用程序标准入口
// 参数：
//   hInstance     — 当前进程实例句柄（用于加载资源）
//   hPrevInstance — 已废弃，始终为 NULL（Win32 每个进程独立）
//   lpCmdLine     — 命令行参数字符串（Unicode）
//   nCmdShow      — 窗口初始显示方式（最大化/最小化/正常）
// =========================
int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
					  _In_opt_ HINSTANCE hPrevInstance,
					  _In_ LPWSTR lpCmdLine,
					  _In_ int nCmdShow)
{
	SetUnhandledExceptionFilter(WriteCrashDump);
	AppendStartupLog("wWinMain entered");
	// 抑制未引用参数警告（hPrevInstance 和 lpCmdLine 在当前设计中不使用）
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	// ===== 阶段1：DPI 感知设置 =====
	// 高 DPI 显示器（如 4K 屏 150%/200% 缩放）下，若不禁用系统缩放，
	// 窗口会被整体拉伸导致模糊。启用 Per-Monitor DPI Awareness v2 后，
	// 由应用程序自己处理缩放，实现像素级精确渲染。
	// 显式设置进程级上下文，避免仅设置线程上下文时 Win32 工作区
	// 仍按系统 DPI 虚拟化，导致 DockSpace 使用逻辑尺寸溢出实际客户区。
	::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	ImGui_ImplWin32_EnableDpiAwareness();
	// 获取主显示器的 DPI 缩放比例（例：100%=1.0, 150%=1.5, 200%=2.0）
	// MonitorFromPoint({0,0}) 始终返回主显示器
	float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
	AppRuntimeState::SetDpiScale(main_scale);  // 存入全局状态，供后续所有 UI 和图像缩放使用

	// ===== 阶段2：创建应用程序窗口 =====
	// ---- 2.1 加载图标资源 ----
	HINSTANCE appInstance = GetModuleHandle(nullptr);  // 获取当前 EXE/DLL 的实例句柄
	// 大图标 32×32（任务栏、Alt+Tab 切换界面使用）
	HICON appIcon = (HICON)::LoadImageW(appInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
	// 小图标 16×16（窗口标题栏左上角使用）
	HICON appIconSmall = (HICON)::LoadImageW(appInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);

	// ---- 2.2 注册窗口类 ----
	// WNDCLASSEXW 定义了窗口的"模板"：图标、光标、背景色、消息处理函数等
	WNDCLASSEXW wc = {
		sizeof(wc),        // cbSize：结构体大小
		CS_CLASSDC,        // style：使用类级 DC（所有窗口共享一个设备上下文）
		WndProc,           // lpfnWndProc：消息处理回调函数指针
		0L, 0L,            // cbClsExtra / cbWndExtra：额外字节（不需要）
		appInstance,       // hInstance：实例句柄
		appIcon,           // hIcon：大图标
		nullptr,           // hCursor：鼠标光标（使用系统默认箭头）
		nullptr,           // hbrBackground：背景画刷（由 DX 渲染覆盖，无需设置）
		nullptr,           // lpszMenuName：菜单名称（ImGui 自己绘制菜单栏）
		L"ImGui Example",  // lpszClassName：窗口类名（创建窗口时用此名称引用）
		appIconSmall       // hIconSm：小图标
	};
	if (!::RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
	{
		ShowStartupError(Win32ErrorMessage("RegisterClassExW", GetLastError()));
		return 1;
	}

	// ---- 2.3 计算窗口尺寸和位置 ----
	// 使用显示器工作区（排除任务栏），而非整个屏幕。
	// 旧代码原点 (100,20) + 全屏尺寸导致窗口右下角超出 1366×768 工业屏边界，
	// 在 DX11 回退模式下尤为明显。
	const HMONITOR primaryMonitor = ::MonitorFromPoint(
		POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
	MONITORINFO monitorInfo{};
	monitorInfo.cbSize = sizeof(monitorInfo);
	::GetMonitorInfoW(primaryMonitor, &monitorInfo);
	const RECT workArea = monitorInfo.rcWork;  // rcWork = 屏幕区域 − 任务栏区域
	const int workW = (std::max)(1L, workArea.right - workArea.left);
	const int workH = (std::max)(1L, workArea.bottom - workArea.top);
	// Per-Monitor DPI Awareness V2 下，Win32 窗口尺寸使用物理像素；
	// 不再把逻辑基准尺寸重复乘以 DPI，否则 125% 缩放时窗口会超出工作区。
	const int desiredW = 1280;
	const int desiredH = 800;
	// 实际窗口大小不超过工作区
	const int winW = (std::min)(desiredW, workW);
	const int winH = (std::min)(desiredH, workH);
	// 居中放置
	const int winX = workArea.left + (workW - winW) / 2;
	const int winY = workArea.top + (workH - winH) / 2;
	// ---- 2.4 创建窗口 ----
	DWORD windowStyle = WS_OVERLAPPEDWINDOW;  // 标准窗口样式：标题栏 + 可调整边框 + 最小/最大/关闭按钮
	HWND hwnd = ::CreateWindowW(
		wc.lpszClassName,           // 窗口类名（必须与 RegisterClassExW 中一致）
		L"IMgui_Opencv Vision",     // 窗口标题（显示在标题栏）
		windowStyle,                 // 窗口样式
		winX, winY, winW, winH,     // 位置和尺寸
		nullptr, nullptr,            // 父窗口、菜单（无）
		wc.hInstance,                // 实例句柄
		nullptr);                    // 创建参数（无）
	if (!hwnd)
	{
		ShowStartupError(Win32ErrorMessage("CreateWindowW", GetLastError()));
		::UnregisterClassW(wc.lpszClassName, wc.hInstance);
		return 1;
	}
	AppRuntimeState::SetWindowHandle(hwnd);  // 保存 HWND 到全局状态，供 GraphicsBackend 等模块获取

	// ---- 2.5 设置 Windows 11 圆角窗口效果 ----
	{
		// DWMWINDOWATTRIBUTE(33) = DWM_WINDOW_CORNER_PREFERENCE（未公开的 enum 值）
		// DWMWCP_ROUND = 2：启用 Windows 11 风格的圆角窗口边框
		const DWORD cornerPreferenceRound = 2;
		::DwmSetWindowAttribute(hwnd, static_cast<DWMWINDOWATTRIBUTE>(33), &cornerPreferenceRound, sizeof(cornerPreferenceRound));
	}

	// ---- 2.6 显示窗口 ----
	::ShowWindow(hwnd, SW_SHOWDEFAULT);  // 按系统默认方式显示（通常是正常大小）
	::UpdateWindow(hwnd);                // 立即发送 WM_PAINT 消息，确保窗口内容首次绘制

	// ===== 阶段4：初始化 Dear ImGui 核心 =====
	// ImGui 是即时模式 GUI 库：无状态保留、无控件对象、每帧重新描述 UI
	IMGUI_CHECKVERSION();            // 编译时版本校验：防止头文件与 .lib 版本不匹配
	ImGui::CreateContext();          // 创建 ImGui 上下文（存储样式、字体、窗口状态等全局数据）
	ImGuiIO &io = ImGui::GetIO();   // IO 结构体：输入事件、显示参数、字体配置的入口
	(void)io;                        // 抑制"已赋值但未使用"的静态分析警告

	// ---- 4.1 启用 ImGui 高级特性 ----
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // 键盘导航：Tab 切换焦点、方向键移动光标
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // 手柄导航：支持 Xbox/PS 手柄操作 UI
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;      // 停靠系统：窗口可拖拽吸附到边缘或组成标签页
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;    // 多视口：窗口可拖出主窗口成为独立 OS 窗口

	// ---- 4.2 配置日志输出路径 ----
	// 日志写入 EXE 所在目录的 imgui_log.txt，而非进程的工作目录
	static char s_LogPath[MAX_PATH];
	GetModuleFileNameA(nullptr, s_LogPath, MAX_PATH);  // 获取 EXE 完整路径
	char *lastSlash = strrchr(s_LogPath, '\\');        // 找到最后一个反斜杠
	if (lastSlash)
		*(lastSlash + 1) = '\0';                        // 截断文件名，保留目录路径
	strcat_s(s_LogPath, "imgui_log.txt");                // 拼接日志文件名
	io.LogFilename = s_LogPath;                          // 设置 ImGui 日志输出文件

	// ---- 4.3 加载 UI 主题和 DPI 适配 ----
	LoadTheme();  // 从 theme.cfg 文件读取上次保存的颜色/样式配置

	ImGuiStyle &style = ImGui::GetStyle();
	style.ScaleAllSizes(main_scale);   // 将所有固定尺寸（边框、间距、圆角等）乘以 DPI 比例
	style.FontScaleDpi = main_scale;   // 字体 DPI 缩放因子
	io.ConfigDpiScaleFonts = true;     // 启用实验性自动字体缩放
	io.ConfigDpiScaleViewports = true; // 多视口模式下也跟随 DPI 变化

	// ---- 4.4 多视口模式下的特殊样式设置 ----
	// 窗口拖出主窗口后，圆角会导致视觉瑕疵，背景不透明度设为 1 避免透明穿透
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		style.WindowRounding = 0.0f;                       // 窗口圆角归零
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;          // 窗口背景 Alpha = 1.0（完全不透明）
	}

	// ---- 4.5 初始化平台后端和图形后端 ----
	// ImGui_ImplWin32_Init：将 ImGui 与 Win32 窗口系统绑定，接管鼠标/键盘输入
	ImGui_ImplWin32_Init(hwnd);

	// GraphicsBackend::Initialize：根据 GPU 能力自动选择 DX12 或 DX11 创建渲染设备
	// ┌─ 优先尝试 DX12（低开销、多线程命令录制）
	// └─ 失败则回退 DX11（兼容性更好、驱动成熟）
	if (!GraphicsBackend::Initialize(hwnd))
	{
		// 图形后端初始化失败 → 记录错误、清理资源、退出进程
		LogSystem::Add(LOG_ERROR, "图形后端初始化失败: %s",
			GraphicsBackend::LastError().c_str());
		ShowStartupError("Graphics initialization failed. " +
			GraphicsBackend::LastError() +
			". Update the display driver or set IMGUI_OPENCV_RENDER_BACKEND=dx11. "
			"DX11 will also attempt WARP software rendering.");
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		::DestroyWindow(hwnd);
		::UnregisterClassW(wc.lpszClassName, wc.hInstance);
		return 1;
	}

	// ===== 阶段5：加载字体（支持中文显示）=====
	// FontManager::InitFonts 加载多个字体合并为 ImGui 字体图集：
	//   1. 默认英文字体（ProggyClean 或 Segoe UI）
	//   2. 中文字体（微软雅黑/宋体，从系统字体目录按优先级查找）
	//   3. 图标字体（FontAwesome，用于工具栏/菜单图标）
	// 所有字形合并到一张 GPU 纹理中，缩放因子 = main_scale
	FontManager::InitFonts(main_scale);
	LogSystem::Add(LOG_INFO, "APP started");  // 应用启动完成标记
	AppendStartupLog(std::string("startup completed; renderer=") +
		GraphicsBackend::Name());

	// ===== 状态变量 =====
	bool done = false;  // 主循环退出标志：收到 WM_QUIT 或渲染失败时置 true

	// ========================================================================
	// 阶段6：主循环 — 消息驱动 + 帧渲染
	// 典型桌面应用的核心模式：
	//   while (!done) {
	//       1. 处理 Windows 消息（输入、窗口事件）
	//       2. 跳过遮挡/最小化帧（省电）
	//       3. 开始 ImGui 新帧
	//       4. 绘制所有 UI 窗口
	//       5. 更新视频/摄像头帧
	//       6. GPU 上传纹理
	//       7. 渲染到屏幕（Present）
	//   }
	// ========================================================================
	while (!done)
	{
		// ----- 6.1 Windows 消息处理 -----
		// PeekMessage + PM_REMOVE：非阻塞方式取出所有排队消息
		// 相比 GetMessage（阻塞等待），PeekMessage 在无消息时立即返回，
		// 使渲染循环能持续运行（对于实时视频预览至关重要）
		MSG msg;
		while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
		{
			::TranslateMessage(&msg);   // 将虚拟键消息转换为字符消息（WM_KEYDOWN → WM_CHAR）
			::DispatchMessage(&msg);    // 调用 WndProc 处理消息
			if (msg.message == WM_QUIT)  // 窗口关闭时 PostQuitMessage 会发送此消息
				done = true;
		}
		if (done)
			break;

		// ----- 6.2 窗口遮挡/最小化优化 -----
		// GraphicsBackend::IsOccluded()：DXGI 交换链检测窗口是否被完全遮挡
		// IsIconic(hwnd)：窗口是否最小化
		// → 两者任一为真则跳过渲染，Sleep(10ms) 降低 CPU 占用
		if (GraphicsBackend::IsOccluded() || ::IsIconic(hwnd))
		{
			::Sleep(10);
			continue;
		}
		// ----- 6.3 开启新的 ImGui 帧 -----
		// 三步启动帧：
		//   1. GraphicsBackend::NewFrame() — DX 后端准备渲染目标
		//   2. ImGui_ImplWin32_NewFrame() — 收集本帧的鼠标/键盘输入事件
		//   3. ImGui::NewFrame() — 初始化 ImGui 内部帧状态（draw list、ID 栈等）
		GraphicsBackend::NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		// ----- 6.3b 延迟日志启动 -----
		// 首帧渲染完成后才开始记录日志到文件，因为需要帧上下文已建立
		{
			static bool s_LogStarted = false;
			if (!s_LogStarted)
			{
				ImGui::LogToFile();    // 开始将日志写入 imgui_log.txt
				s_LogStarted = true;
			}
		}

		// =========================
		// 6.4 UI 绘制 — 所有 ImGui 窗口的渲染调用
		// 顺序很重要：后绘制的窗口会覆盖在先绘制的上面
		// =========================
		UI::DrawDockSpaceHost();       // ① 主停靠空间 + 菜单栏（作为背景容器最先绘制）
		UI::ShowSidebar();             // ② 侧边栏控制面板（图片加载/保存/ROI选择）
		UI::ShowLogWindow();           // ③ 日志窗口
		UI::ShowStatsWindow();         // ④ 性能统计窗口（帧率/内存/GPU 时间）
		UI::ShowOpenCV();              // ⑤ 图片显示窗口（中央大图 + 缩放/平移）
		UI::ShowToolsWindow();         // ⑥ 工具窗口（ROI管理+算法入口+参数面板）
		UI::ShowRunResultWindow();     // ⑦ 运行结果看板（整链执行完成后的汇总）
		UI::ShowHardwareWindow();      // ⑧ 设备连接页（覆盖在所有内容之上，必须最后绘制）

		// ----- 6.5 渲染绘制数据到 GPU 命令列表 -----
		// ImGui::Render() 将所有窗口的 DrawList 转换为顶点缓冲 + 索引缓冲，
		// 并生成相应的绘制命令（纹理切换、裁剪矩形）
		ImGui::Render();

		// =========================
		// 6.6 视频/摄像头帧更新 — 在 GPU 上传之前捕获最新帧
		// VideoCapture::Update() 从相机/视频文件抓取一帧存入 ImageState
		// HardwareRuntimeService::Tick() 处理 PLC 通信心跳、硬件状态轮询
		// =========================
		try
		{
			VideoCapture::Update();
			HardwareRuntimeService::Tick();
		}
		catch (const cv::Exception &e)
		{
			LogSystem::Add(LOG_ERROR, "视频帧异常: %s", e.what());
		}
		catch (...)
		{
			LogSystem::Add(LOG_ERROR, "视频帧未知异常");
		}

		// =========================
		// 6.7 YOLO 实时检测 — 对当前帧运行 ONNX 目标检测
		// LiveYoloRunner::Update() 异步提交推理，结果叠加到预览图像
		// =========================
		LiveYoloRunner::Update();

		// =========================
		// 6.8 图片异步加载调度 — 检查是否有后台解码完成的图片
		// ImageLoadController::Update() 轮询异步加载完成事件，
		// 触发回调将解码后的 cv::Mat 交付给 ImageState
		// =========================
		ImageLoadController::Update();

		// =========================
		// 6.9 GPU 纹理上传 — 将 CPU 端的 cv::Mat 像素数据上传到 GPU 显存
		// 只在图像数据发生变化（脏标记）时才执行上传，避免每帧重复传输
		// =========================
		if (ImageState::NeedUploadRef())  // 检查脏标记：图像内容是否被修改
		{
			// UploadMainTexture：创建/更新主纹理的 D3D 资源（纹理2D + SRV）
			if (GraphicsBackend::UploadMainTexture(ImageState::PendingUploadRef()))
				ImageState::NeedUploadRef() = false;  // 上传成功，清除脏标记
		}
		PreviewTextureCache::UploadPending();  // 上传预览缩略图缓存中的待处理纹理

		// ----- 6.10 交换链呈现 — 将渲染结果提交到屏幕 -----
		// GraphicsBackend::RenderAndPresent 内部流程：
		//   1. 执行 GPU 命令列表（绘制 ImGui 三角形 + 图像纹理）
		//   2. Present() 将后台缓冲区翻转到前台（V-Sync 或 tearing）
		//   3. 返回 false 表示设备丢失（如显卡驱动崩溃），触发退出
		if (!GraphicsBackend::RenderAndPresent(clear_color, io))
			done = true;
	}

	// ========================================================================
	// 清理阶段 — 按依赖关系的逆序释放资源
	// 黄金法则：先创建的后释放，避免悬空指针和使用已释放资源
	// ========================================================================
	LogSystem::Add(LOG_INFO, "APP shutdown begin");

	// ---- 7.1 等待 GPU 完成所有待处理命令 ----
	// 防止释放资源时 GPU 仍在访问（导致 TDR 或崩溃）
	GraphicsBackend::WaitIdle();

	// ---- 7.2 关闭业务模块（先于 ImGui 和图形后端销毁）----
	// 保存未保存的配方修改
	if (UI::IsCurrentRecipeDirty())
		UI::SaveCurrentRecipe();
	RecipeAutosaveService::Shutdown();   // 停止自动保存定时器
	ToolController::Shutdown();          // 销毁所有算法工具实例
	HardwareRuntimeService::Shutdown();  // 断开 PLC/相机等硬件连接
	VideoCapture::Close();               // 释放摄像头/视频文件
	PreviewTextureCache::Shutdown();     // 清理预览纹理缓存

	// ---- 7.3 清理 ImGui 和图形后端 ----
	ImGui::LogFinish();                  // 停止日志记录
	GraphicsBackend::Shutdown();         // 释放 DX12/DX11 设备、交换链、描述符堆
	ImGui_ImplWin32_Shutdown();          // 解除 Win32 输入挂钩
	ImGui::DestroyContext();             // 销毁 ImGui 上下文（释放字体纹理、样式数据）

	// ---- 7.4 销毁窗口和窗口类 ----
	::DestroyWindow(hwnd);
	::UnregisterClassW(wc.lpszClassName, wc.hInstance);

	LogSystem::Add(LOG_INFO, "APP shutdown end");

	return 0;  // 正常退出
}

// 前向声明 Win32 消息处理器（来自 imgui_impl_win32.cpp 的实现）
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
// ImGui 内部维护了 Win32 输入处理的全局状态，此函数将鼠标/键盘/游戏手柄
// 事件翻译为 ImGuiIO 中的对应字段
// =========================
// Win32 窗口消息处理函数 — 所有窗口消息的入口
//
// 核心设计原则：
//   所有输入事件（鼠标移动/点击、键盘按键、滚轮、触摸等）先交给
//   ImGui_ImplWin32_WndProcHandler 处理，由 ImGui 决定是否需要"消费"该事件。
//   - io.WantCaptureMouse = true  → 鼠标事件被 ImGui 窗口捕获，不下发到应用
//   - io.WantCaptureKeyboard = true → 键盘事件被 ImGui 输入框捕获
//   - 两者均为 false → 事件不是给 ImGui 的，传递给 DefWindowProc 默认处理
// =========================
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	// 步骤1：先将事件交给 ImGui，如果 ImGui 消费了（返回 true），则不再往下传递
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	// 步骤2：ImGui 未消费的事件，由应用程序自行处理
	switch (msg)
	{
	case WM_SIZE:
		// 窗口尺寸改变时，通知图形后端重建交换链和渲染目标
		// SIZE_MINIMIZED 时不处理（避免为最小化的窗口创建 0×0 的渲染目标）
		if (GraphicsBackend::IsInitialized() && wParam != SIZE_MINIMIZED)
			GraphicsBackend::Resize(
				static_cast<unsigned int>(LOWORD(lParam)),  // 新宽度（客户区）
				static_cast<unsigned int>(HIWORD(lParam))); // 新高度（客户区）
		return 0;

	case WM_SYSCOMMAND:
		// 拦截 Alt 键激活菜单栏的默认行为（SC_KEYMENU 是系统菜单命令）
		// 因为 ImGui 使用自己的菜单栏（通过停靠空间绘制），不需要 Windows 默认菜单
		if ((wParam & 0xfff0) == SC_KEYMENU)
			return 0;
		break;

	case WM_DESTROY:
		// 窗口被销毁时，发送 WM_QUIT 消息到消息队列，触发主循环退出
		LogSystem::Add(LOG_INFO, "WM_DESTROY received");
		::PostQuitMessage(0);
		return 0;
	}

	// 步骤3：未处理的消息交给 Windows 默认处理（窗口拖动、标题栏按钮等）
	return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
