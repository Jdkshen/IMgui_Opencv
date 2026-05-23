#pragma once
#include "framework.h"

#include "imgui/imconfig.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx12.h"
#include "imgui/imgui_internal.h"
#include "imgui/imstb_rectpack.h"
#include "imgui/imstb_textedit.h"
#include "imgui/imstb_truetype.h"

// 核心模块
#include "Core/DX12Context.h"
#include "Core/OpenCVTest.h"
#include "Core/OpenFileDialog.h"
#include "Core/ThemeManager.h"
#include "Core/AsyncImageLoader.h"

// 渲染与日志
#include "Renderer/FontManager.h"
#include "Log/LogSystem.h"

// UI 模块
#include "UI/DockSpaceHost.h"
#include "UI/ImageViewer.h"
#include "UI/ROIManager.h"
#include "UI/LogWindow.h"
#include "UI/Sidebar.h"
#include "UI/StatsWindow.h"
#include "UI/ToolsWindow.h"

// 算法模块
#include "Algorithm/ThresholdTool.h"
#include "Algorithm/TemplateMatch.h"





