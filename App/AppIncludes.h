#pragma once
#include "Platform.h"

#include "imgui/imconfig.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_internal.h"
#include "imgui/imstb_rectpack.h"
#include "imgui/imstb_textedit.h"
#include "imgui/imstb_truetype.h"

// 核心模块
#include "Core/OpenFileDialog.h"
#include "Core/ThemeManager.h"
#include "Core/AsyncImageLoader.h"
#include "Core/VideoCapture.h"
#include "Core/RecipeManager.h"
#include "Core/ImageUtils.h"
#include "Core/VisionContext.h"
#include "Core/ToolExecutor.h"
#include "Core/ToolController.h"
#include "Core/HardwareRuntimeService.h"

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
#include "UI/HardwareWindow.h"

// 算法模块
#include "Algorithm/ThresholdTool.h"



