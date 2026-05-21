#include "../Windows_imgui.h"
#include "ImageViewer.h"
#include "ROIManager.h"
#include "../Core/OpenFileDialog.h"
#include "../Core/DX12Context.h"
#include "../Log/LogSystem.h"

extern std::string pendingPath;

// 图像显示/视图变换状态定义
float  gZoom           = 1.0f;
ImVec2 gPan            = ImVec2(0, 0);
ImVec2 gCanvasSize;
ImVec2 gImageScreenPos;
ImVec2 imageScreenPos;

namespace UI
{

	void ShowOpenCV()
	{
		if (!g_ShowOpenCV) return;

		ImGui::Begin("图像预览", &g_ShowOpenCV);

		float buttonWidth = 100.0f;

		if (ImGui::Button("放大"))
			ZoomAtCenter(0.1f);
		ImGui::SameLine();
		if (ImGui::Button("缩小"))
			ZoomAtCenter(-0.1f);
		ImGui::SameLine();
		if (ImGui::Button("适合窗口"))
			FitImageToWindow();
		ImGui::SameLine();
		if (ImGui::Button("清空ROI"))
		{
			gROIs.clear();
			TemplateMatch::Clear();
			gDrawingROI = false;
		}
		ImGui::SameLine();
		if (ImGui::Button("打印ROI"))
			PrintROIToLog();
		ImGui::SameLine();
		if (ImGui::Button("清理图片"))
			ClearImage();
		ImGui::SameLine();

		if (ImGui::Button("选择图片", ImVec2(buttonWidth, 0)))
		{
			pendingPath = OpenFileDialog();
			if (!pendingPath.empty())
			{
				LogSystem::Add(LOG_INFO, color, "选择图片路径: %s", pendingPath.c_str());
				OutputDebugStringA(("UI gDevice=" + std::to_string((uintptr_t)gDevice) + "\n").c_str());
				OutputDebugStringA(("UI gCmdList=" + std::to_string((uintptr_t)gCmdList) + "\n").c_str());
			}
			else
			{
				LogSystem::Add(LOG_WARN, color, "选择图片 - 用户取消了选择或路径为空");
			}
		}

		ImGui::Separator();

		ImGui::BeginChild("ImageRegion", ImVec2(0, 0), true,
			ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		gCanvasSize = ImGui::GetContentRegionAvail();

		if (ImGui::IsWindowHovered())
		{
			float wheel = ImGui::GetIO().MouseWheel;
			if (wheel != 0.0f)
			{
				ImVec2 mousePos = ImGui::GetMousePos();
				float imageX = (mousePos.x - imageScreenPos.x) / gZoom;
				float imageY = (mousePos.y - imageScreenPos.y) / gZoom;
				float oldZoom = gZoom;
				gZoom += wheel * 0.1f;
				gZoom = std::clamp(gZoom, 0.1f, 20.0f);
				gPan.x -= imageX * (gZoom - oldZoom);
				gPan.y -= imageY * (gZoom - oldZoom);
			}
		}

		if (!gDraggingROI && gActiveHandle == HANDLE_NONE &&
			ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			ImVec2 delta = ImGui::GetIO().MouseDelta;
			gPan.x += delta.x;
			gPan.y += delta.y;
		}

		if (gTexture && gSrvGpuHandle.ptr != 0)
		{
			float drawW = gImageWidth * gZoom;
			float drawH = gImageHeight * gZoom;
			imageScreenPos = ImGui::GetCursorScreenPos();
			ImVec2 drawPos = ImVec2(imageScreenPos.x + gPan.x, imageScreenPos.y + gPan.y);
			ImGui::SetCursorScreenPos(drawPos);
			ImGui::Image((ImTextureID)gSrvGpuHandle.ptr, ImVec2(drawW, drawH));
		}

		if (!gTexture || gImageWidth <= 0 || gImageHeight <= 0)
			ImGui::Text("暂无图片");

		HandleROIInteraction();
		ImGui::EndChild();
		ImGui::End();
	}

	void FitImageToWindow()
	{
		if (gImageWidth <= 0 || gImageHeight <= 0) return;

		float regionW = gCanvasSize.x;
		float regionH = gCanvasSize.y;
		float scaleX = regionW / (float)gImageWidth;
		float scaleY = regionH / (float)gImageHeight;
		gZoom = (scaleX < scaleY) ? scaleX : scaleY;
		if (gZoom > 1.0f) gZoom = 1.0f;
		float drawW = gImageWidth * gZoom;
		float drawH = gImageHeight * gZoom;
		gPan.x = (regionW - drawW) * 0.5f;
		gPan.y = (regionH - drawH) * 0.5f;
	}

	void ClearImage()
	{
		gImageWidth = 0;
		gImageHeight = 0;
		ClearROIState();
		TemplateMatch::Clear();
		gZoom = 1.0f;
		gPan = ImVec2(0, 0);
		imageScreenPos = ImVec2(0, 0);
	}

}
