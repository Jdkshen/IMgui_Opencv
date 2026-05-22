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

// 图片列表浏览状态
std::vector<std::string> gImageList;
int                      gCurrentImageIndex = -1;

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

		ImGui::Separator();

		// 为底部浏览工具栏预留空间（分隔线 + 按钮行约 35px）
		ImGui::BeginChild("ImageRegion", ImVec2(0, -35.0f), true,
			ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		gCanvasSize = ImGui::GetContentRegionAvail();

		// 记录图片区域屏幕范围（用于限制像素坐标显示）
		ImVec2 childScreenPos = ImGui::GetCursorScreenPos();
		ImVec2 childScreenSize = ImGui::GetContentRegionAvail();

		// 鼠标滚轮缩放（以鼠标尖端为锚点，图片自动跟随）
		if (ImGui::IsWindowHovered())
		{
			float wheel = ImGui::GetIO().MouseWheel;
			if (wheel != 0.0f)
			{
				ImVec2 mousePos = ImGui::GetMousePos();
				// 鼠标指向的图片像素坐标（考虑 pan 偏移）
				float imgScreenX = imageScreenPos.x + gPan.x;
				float imgScreenY = imageScreenPos.y + gPan.y;
				float imageX = (mousePos.x - imgScreenX) / gZoom;
				float imageY = (mousePos.y - imgScreenY) / gZoom;
				float oldZoom = gZoom;
				gZoom += wheel * 0.1f;
				gZoom = std::clamp(gZoom, 0.005f, 50.0f);  // 最小0.5%
				// 调整 pan 使鼠标指向的像素位置不变
				gPan.x -= imageX * (gZoom - oldZoom);
				gPan.y -= imageY * (gZoom - oldZoom);
			}
		}

		// 左键拖拽平移（未拖动ROI时）
		if (!gDraggingROI && gActiveHandle == HANDLE_NONE &&
			ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			ImVec2 delta = ImGui::GetIO().MouseDelta;
			gPan.x += delta.x;
			gPan.y += delta.y;
		}

		// 绘制DX12纹理（图片显示）
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

		// 处理ROI交互 + 绘制匹配结果
		HandleROIInteraction();
		ImGui::EndChild();

		ImGui::Separator();

		// ===== 图片浏览工具栏：文件夹 / 上一张 / 下一张 / 选择图片 =====
		if (ImGui::Button("选择文件夹", ImVec2(buttonWidth, 0)))
		{
			std::string folderPath = OpenFolderDialog();
			if (!folderPath.empty())
			{
				LoadFolderImages(folderPath);
				LogSystem::Add(LOG_INFO, color, "加载文件夹: %s, 共 %zu 张图片",
					folderPath.c_str(), gImageList.size());
			}
		}
		ImGui::SameLine();

		// 上一张按钮（只有列表非空且有上一张时可用）
		bool hasPrev = (!gImageList.empty() && gCurrentImageIndex > 0);
		if (!hasPrev) ImGui::BeginDisabled();
		if (ImGui::Button("上一张", ImVec2(buttonWidth, 0)))
			NavigatePrevImage();
		if (!hasPrev) ImGui::EndDisabled();
		ImGui::SameLine();

		// 下一张按钮
		bool hasNext = (!gImageList.empty() && gCurrentImageIndex >= 0 &&
		                gCurrentImageIndex < (int)gImageList.size() - 1);
		if (!hasNext) ImGui::BeginDisabled();
		if (ImGui::Button("下一张", ImVec2(buttonWidth, 0)))
			NavigateNextImage();
		if (!hasNext) ImGui::EndDisabled();
		ImGui::SameLine();

		// 图片计数显示
		if (!gImageList.empty() && gCurrentImageIndex >= 0)
		{
			ImGui::Text(" %d / %zu ", gCurrentImageIndex + 1, gImageList.size());
		}
		else
		{
			ImGui::Text(" 无列表 ");
		}
		ImGui::SameLine();

		if (ImGui::Button("选择图片", ImVec2(buttonWidth, 0)))
		{
			pendingPath = OpenFileDialog();
			if (!pendingPath.empty())
			{
				LogSystem::Add(LOG_INFO, color, "选择图片路径: %s", pendingPath.c_str());
				OutputDebugStringA(("UI gDevice=" + std::to_string((uintptr_t)gDevice) + "\n").c_str());
				OutputDebugStringA(("UI gCmdList=" + std::to_string((uintptr_t)gCmdList) + "\n").c_str());
				// 单独选择图片时清空列表模式
				gImageList.clear();
				gCurrentImageIndex = -1;
			}
			else
			{
				LogSystem::Add(LOG_WARN, color, "选择图片 - 用户取消了选择或路径为空");
			}
		}

		// ===== 右侧信息栏：尺寸 | 格式 | 像素坐标 =====
		{
			ImGui::SameLine();
			if (!gImage.empty())
			{
				const char* fmtStr = "?";
				int ch = gImage.channels();
				if (ch == 1) fmtStr = "Gray";
				else if (ch == 3) fmtStr = "BGR";
				else if (ch == 4) fmtStr = "BGRA";
				// 仅鼠标在图片窗口范围内才显示像素坐标
				ImVec2 mouse = ImGui::GetMousePos();
				bool inChild = (mouse.x >= childScreenPos.x && mouse.x < childScreenPos.x + childScreenSize.x &&
				                mouse.y >= childScreenPos.y && mouse.y < childScreenPos.y + childScreenSize.y);
				if (inChild)
				{
					ImVec2 imgCoord = ScreenToImagePos(mouse);
					bool inImg = (imgCoord.x >= 0 && imgCoord.x < gImageWidth &&
					              imgCoord.y >= 0 && imgCoord.y < gImageHeight);
					if (inImg)
						ImGui::TextDisabled("%dx%d %s | X:%.0f Y:%.0f",
							gImageWidth, gImageHeight, fmtStr, imgCoord.x, imgCoord.y);
					else
						ImGui::TextDisabled("%dx%d %s | X:--- Y:---",
							gImageWidth, gImageHeight, fmtStr);
				}
				else
				{
					ImGui::TextDisabled("%dx%d %s", gImageWidth, gImageHeight, fmtStr);
				}
			}
		}

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

	// =====================================================
	// 从文件夹加载所有图片
	// =====================================================
	void LoadFolderImages(const std::string& folderPath)
	{
		gImageList = ScanImageFiles(folderPath);
		gCurrentImageIndex = -1;

		if (gImageList.empty())
		{
			LogSystem::Add(LOG_WARN, color, "文件夹中没有找到图片文件");
			return;
		}

		// 加载第一张
		NavigateToImage(0);
	}

	// =====================================================
	// 切换到指定索引的图片
	// =====================================================
	void NavigateToImage(int index)
	{
		if (gImageList.empty() || index < 0 || index >= (int)gImageList.size())
			return;

		gCurrentImageIndex = index;

		// 清除上一次的 ROI 和模板匹配结果
		ClearROIState();
		TemplateMatch::Clear();

		// 通过 pendingPath 机制触发图片加载
		pendingPath = gImageList[index];

		LogSystem::Add(LOG_INFO, color, "切换到图片 [%d/%zu]: %s",
			index + 1, gImageList.size(), gImageList[index].c_str());
	}

	// =====================================================
	// 上一张
	// =====================================================
	void NavigatePrevImage()
	{
		if (gCurrentImageIndex > 0)
			NavigateToImage(gCurrentImageIndex - 1);
	}

	// =====================================================
	// 下一张
	// =====================================================
	void NavigateNextImage()
	{
		if (gCurrentImageIndex >= 0 && gCurrentImageIndex < (int)gImageList.size() - 1)
			NavigateToImage(gCurrentImageIndex + 1);
	}

}
