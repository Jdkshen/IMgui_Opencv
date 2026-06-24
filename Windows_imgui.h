#pragma once
#include "framework.h"

extern float g_DPIScale;          // DPI 缂╂斁姣斾緥

extern HWND g_hWnd;

#include "imgui/imconfig.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx12.h"
#include "imgui/imgui_internal.h"
#include "imgui/imstb_rectpack.h"
#include "imgui/imstb_textedit.h"
#include "imgui/imstb_truetype.h"

// 鏍稿績妯″潡
#include "Core/DX12Context.h"
#include "Core/OpenCVTest.h"
#include "Core/OpenFileDialog.h"
#include "Core/ThemeManager.h"
#include "Core/AsyncImageLoader.h"
#include "Core/VideoCapture.h"
#include "Core/RecipeManager.h"
#include "Core/ImageUtils.h"
#include "Core/VisionContext.h"
#include "Core/ToolExecutor.h"
#include "Core/ToolController.h"

// 娓叉煋涓庢棩蹇?
#include "Renderer/FontManager.h"
#include "Log/LogSystem.h"

// UI 妯″潡
#include "UI/DockSpaceHost.h"
#include "UI/ImageViewer.h"
#include "UI/ROIManager.h"
#include "UI/LogWindow.h"
#include "UI/Sidebar.h"
#include "UI/StatsWindow.h"
#include "UI/ToolsWindow.h"

// 绠楁硶妯″潡
#include "Algorithm/ThresholdTool.h"
#include "Algorithm/TemplateMatch.h"



