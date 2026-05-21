#pragma once
#include "framework.h"






#include "imgui/imconfig.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_dx12.h"
#include "imgui/imgui_internal.h"
#include "imgui/imstb_rectpack.h"
#include "imgui/imstb_textedit.h"
#include "imgui/imstb_truetype.h"
#include "UI/DockSpaceHost.h"
#include "UI/ROIManager.h"
#include "UI/ImageViewer.h"
#include "Renderer/FontManager.h"
#include "Log/LogSystem.h"
#include "Core/OpenFileDialog.h"
#include "Core/OpenCVTest.h"
#include "Core/DX12Context.h"
#include "Algorithm/ThresholdTool.h"
#include "Algorithm/TemplateMatch.h"

#include "Core/ThemeManager.h"

// =========================
// 主窗口句柄（extern 声明，定义在 Windows_imgui.cpp）





