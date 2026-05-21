#include "../Windows_imgui.h"
#include "ImageViewer.h"
#include "ROIManager.h"
#include "../OpenCV/TemplateMatch.h"
#include "../Core/OpenFileDialog.h"
#include "../Core/DX12Context.h"
#include "../log/LogSystem.h"

extern std::string pendingPath;

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

		ImageToScreen();
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

	void ImageToScreen()
	{
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImVec2 mouse = ImGui::GetMousePos();
		ImVec2 imageMouse = ScreenToImagePos(mouse);

		if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
		{
			gDrawingROI = true;
			gROIStart = imageMouse;
		}
		if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
		{
			if (gDrawingROI)
			{
				ROI roi;
				roi.start = gROIStart;
				roi.end = imageMouse;
				NormalizeROI(roi);
				if (fabs(roi.start.x - roi.end.x) > 2 && fabs(roi.start.y - roi.end.y) > 2)
					gROIs.push_back(roi);
			}
			gDrawingROI = false;
		}

		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			gDraggingROI = false;
			gActiveHandle = HANDLE_NONE;
		}

		struct Box { ImVec2 lt, rt, lb, rb, t, b, l, r, c; };
		auto GetBox = [&](const ROI& roi) -> Box
		{
			float minX = std::min(roi.start.x, roi.end.x);
			float maxX = std::max(roi.start.x, roi.end.x);
			float minY = std::min(roi.start.y, roi.end.y);
			float maxY = std::max(roi.start.y, roi.end.y);
			return Box{
				{minX,minY},{maxX,minY},{minX,maxY},{maxX,maxY},
				{(minX+maxX)*0.5f,minY},{(minX+maxX)*0.5f,maxY},
				{minX,(minY+maxY)*0.5f},{maxX,(minY+maxY)*0.5f},
				{(minX+maxX)*0.5f,(minY+maxY)*0.5f}
			};
		};

		auto CheckHandle = [&](ImVec2 p, HandleType type, int i) -> bool
		{
			ImVec2 sp = ImageToScreenPos(p);
			float dx = mouse.x - sp.x;
			float dy = mouse.y - sp.y;
			if (sqrtf(dx*dx + dy*dy) < HANDLE_SIZE * 2.0f)
			{ gSelectedROI = i; gActiveHandle = type; return true; }
			return false;
		};

		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			gSelectedROI = -1;
			gActiveHandle = HANDLE_NONE;

			for (int i = 0; i < (int)gROIs.size(); i++)
			{
				auto& roi = gROIs[i];
				Box box = GetBox(roi);

				if (CheckHandle(box.lt, HANDLE_LT, i)) break;
				if (CheckHandle(box.rt, HANDLE_RT, i)) break;
				if (CheckHandle(box.lb, HANDLE_LB, i)) break;
				if (CheckHandle(box.rb, HANDLE_RB, i)) break;
				if (CheckHandle(box.t, HANDLE_T, i)) break;
				if (CheckHandle(box.b, HANDLE_B, i)) break;
				if (CheckHandle(box.l, HANDLE_L, i)) break;
				if (CheckHandle(box.r, HANDLE_R, i)) break;
				if (CheckHandle(box.c, HANDLE_CENTER, i)) break;

				float minX = std::min(roi.start.x, roi.end.x);
				float maxX = std::max(roi.start.x, roi.end.x);
				float minY = std::min(roi.start.y, roi.end.y);
				float maxY = std::max(roi.start.y, roi.end.y);

				if (imageMouse.x >= minX && imageMouse.x <= maxX &&
					imageMouse.y >= minY && imageMouse.y <= maxY)
				{ gSelectedROI = i; break; }
			}
		}

		if (gSelectedROI >= 0 && ImGui::IsKeyPressed(ImGuiKey_Delete))
		{
			gROIs.erase(gROIs.begin() + gSelectedROI);
			gSelectedROI = -1;
			gActiveHandle = HANDLE_NONE;
			gDraggingROI = false;
		}

		if (gActiveHandle != HANDLE_NONE && gSelectedROI >= 0)
		{
			auto& roi = gROIs[gSelectedROI];

			if (gActiveHandle >= HANDLE_T)
			{
				if (!gDraggingROI) { gDraggingROI = true; gLastMousePos = imageMouse; }
				ImVec2 delta(imageMouse.x - gLastMousePos.x, imageMouse.y - gLastMousePos.y);
				roi.start.x += delta.x; roi.start.y += delta.y;
				roi.end.x += delta.x;   roi.end.y += delta.y;
				gLastMousePos = imageMouse;
			}
			else switch (gActiveHandle)
			{
			case HANDLE_LT: roi.start = imageMouse; break;
			case HANDLE_RB: roi.end = imageMouse; break;
			case HANDLE_RT: roi.start.y = imageMouse.y; roi.end.x = imageMouse.x; break;
			case HANDLE_LB: roi.start.x = imageMouse.x; roi.end.y = imageMouse.y; break;
			}

			if (gActiveHandle < HANDLE_T) NormalizeROI(roi);
		}

		for (int i = 0; i < (int)gROIs.size(); i++)
		{
			auto& roi = gROIs[i];
			ImU32 col = (i == gSelectedROI) ? IM_COL32(255,0,0,255) : IM_COL32(0,255,0,255);
			ImVec2 p1 = ImageToScreenPos(roi.start);
			ImVec2 p2 = ImageToScreenPos(roi.end);
			drawList->AddRect(p1, p2, col, 0, 0, 2.0f);

			ImVec2 pc = ImageToScreenPos(ImVec2(
				(roi.start.x+roi.end.x)*0.5f, (roi.start.y+roi.end.y)*0.5f));
			drawList->AddCircleFilled(pc, 4.0f, col);
			drawList->AddCircle(pc, 4.0f, IM_COL32(255,255,255,255), 0, 1.0f);
		}

		TemplateMatch::DrawMatches(drawList);

		if (gDrawingROI)
		{
			ImVec2 p1 = ImageToScreenPos(gROIStart);
			ImVec2 p2 = ImageToScreenPos(imageMouse);
			drawList->AddRect(p1, p2, IM_COL32(255,255,0,255));
		}
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
