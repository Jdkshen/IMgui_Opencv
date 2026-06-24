#include "../Windows_imgui.h"
#include "ImageViewer.h"
#include "ROIManager.h"
#include "../Core/OpenFileDialog.h"
#include "../Core/DX12Context.h"
#include "../Core/FrameSourceState.h"
#include "../Core/VideoCapture.h"
#include "../Core/VisionContext.h"
#include "../Algorithm/YOLODetector.h"
#include "../Algorithm/ITool.h"
#include "../Log/LogSystem.h"

extern std::string pendingPath;

// 图像显示/视图变换状态定义
float gZoom = 1.0f;
ImVec2 gPan = ImVec2(0, 0);
ImVec2 gCanvasSize;
ImVec2 gImageScreenPos;
ImVec2 imageScreenPos;
bool g_ShowPixelGrid = false; // 像素网格开关
bool g_ShowCoordGrid = false; // 坐标网格开关
int g_GridStep = 1;			  // 坐标网格步长（图片像素）

// 图片列表浏览状态
std::vector<std::string> gImageList;
int gCurrentImageIndex = -1;

// 旧叠加层：仅保留 YOLO（有视频偏移补偿逻辑）和统一结果
std::vector<DetectedObject> g_YoloOverlays;
bool g_YoloShowOverlay = false;
float g_YoloOverlayOffsetX = 0.0f;

int g_ImageVersion = 0;

// ---- 统一结果叠加层（唯一结果源） ----
static void DrawUnifiedResults(ImDrawList* dl)
{
    const auto& results = gContext.unifiedResults;
    if (results.empty()) return;
    static const ImU32 cols[] = {
        IM_COL32(0,255,0,255), IM_COL32(255,128,0,255), IM_COL32(0,128,255,255),
        IM_COL32(255,0,255,255), IM_COL32(0,255,255,255),
    };
    constexpr int nCol = 5;
    for (size_t i = 0; i < results.size(); i++)
    {
        const auto& r = results[i];
        if (!r.success) continue;
        float t = std::max(2.0f, 3.0f * gZoom);

        // 检测框
        for (const auto& d : r.detections) {
            auto p1 = UI::ImageToScreenPos(ImVec2((float)d.box.x, (float)d.box.y));
            auto p2 = UI::ImageToScreenPos(ImVec2((float)(d.box.x+d.box.width), (float)(d.box.y+d.box.height)));
            dl->AddRect(p1, p2, cols[i % nCol], 0, 0, t);
            if (!d.label.empty()) {
                char buf[128]; snprintf(buf, sizeof(buf), "%s %.2f", d.label.c_str(), d.score);
                dl->AddText(ImVec2(p1.x+2, p1.y-ImGui::GetFontSize()-2), IM_COL32(255,255,255,255), buf);
            }
        }

        // 区域（轮廓多边形 + 顶点圆点）
        for (const auto& reg : r.regions) {
            auto p1 = UI::ImageToScreenPos(ImVec2((float)reg.bbox.x, (float)reg.bbox.y));
            auto p2 = UI::ImageToScreenPos(ImVec2((float)(reg.bbox.x+reg.bbox.width), (float)(reg.bbox.y+reg.bbox.height)));

            // 部分匹配：分数<1 且有 area 标记 → 不画框，只画绿/红混色点
            bool isPartial = (reg.score < 1.0f && reg.area > 0 && reg.area < (float)reg.contour.size());
            int matchedN = isPartial ? (int)reg.area : (int)reg.contour.size();
            ImU32 rectColor = cols[i % nCol];
            ImU32 greenDot  = IM_COL32(0, 255, 0, 255);
            ImU32 redDot    = IM_COL32(255, 60, 60, 255);

            // 部分匹配不画框，只画点
            if (!isPartial)
            {
                dl->AddRect(p1, p2, rectColor, 0, 0, t);
                if (!reg.label.empty()) {
                    char buf[128]; snprintf(buf, sizeof(buf), "%s", reg.label.c_str());
                    dl->AddText(ImVec2(p1.x+2, p1.y-ImGui::GetFontSize()-2), IM_COL32(255,255,255,255), buf);
                }
            }
            // 轮廓顶点：画圆点（部分匹配时绿/红区分）
            float dotR = std::max(3.0f, 5.0f * gZoom);
            for (int ci = 0; ci < (int)reg.contour.size(); ci++) {
                auto cp = UI::ImageToScreenPos(ImVec2((float)reg.contour[ci].x, (float)reg.contour[ci].y));
                ImU32 dotColor = isPartial ? (ci < matchedN ? greenDot : redDot) : cols[i % nCol];
                dl->AddCircleFilled(cp, dotR, dotColor);
                dl->AddCircle(cp, dotR, IM_COL32(255,255,255,180), 0, std::max(1.0f, gZoom));
            }
            // 轮廓顶点多边形（2+ 点连线）— 部分匹配不画连线
            if (!isPartial && reg.contour.size() >= 2)
            {
                std::vector<ImVec2> screenPts;
                screenPts.reserve(reg.contour.size());
                for (const auto& pt : reg.contour)
                    screenPts.push_back(UI::ImageToScreenPos(ImVec2((float)pt.x, (float)pt.y)));
                dl->AddPolyline(screenPts.data(), (int)screenPts.size(), cols[i % nCol], ImDrawFlags_Closed, t);
            }
        }

        // 线段
        for (const auto& l : r.lines) {
            auto p1 = UI::ImageToScreenPos(ImVec2((float)l.p1.x, (float)l.p1.y));
            auto p2 = UI::ImageToScreenPos(ImVec2((float)l.p2.x, (float)l.p2.y));
            dl->AddLine(p1, p2, cols[i % nCol], t);
        }
    }
}

namespace UI
{

	void ShowOpenCV()
	{
		if (!g_ShowOpenCV)
			return;

				ImGui::Begin("图像预览", &g_ShowOpenCV,
					ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

			float buttonWidth = 100.0f;

				auto ToolbarLabel = [](const char* label)
				{
					ImGui::TextDisabled("%s", label);
					ImGui::SameLine();
			};

			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 4));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5, 4));

				ToolbarLabel("视图");
				if (ImGui::Button("放大"))
					ZoomAtCenter(0.1f);
				ImGui::SameLine();
				if (ImGui::Button("缩小"))
					ZoomAtCenter(-0.1f);
				ImGui::SameLine();
				if (ImGui::Button("适合窗口"))
					FitImageToWindow();
				ImGui::SameLine();
				ToolbarLabel("ROI");
				if (ImGui::Button("清空ROI"))
				{
				gROIs.clear();
				TemplateMatch::Clear();
			gDrawingROI = false;
			g_YoloOverlays.clear();
					g_YoloShowOverlay = false;
						gContext.ClearUnifiedResults();
				}
				ImGui::SameLine();
				if (ImGui::Button("打印ROI"))
					PrintROIToLog();
				ImGui::SameLine();
				if (ImGui::Button("清理图片"))
					ClearImage();

				ToolbarLabel("采集");
				// 打开视频文件
				if (ImGui::Button("打开视频"))
			{
				std::string path = OpenVideoDialog();
			if (!path.empty())
			{
						VideoCapture::OpenVideo(path);
					}
				}
				ImGui::SameLine();
				// 打开摄像头
				if (ImGui::Button("打开摄像头"))
				{
					VideoCapture::OpenCamera(0);
				}
				ImGui::SameLine();
				ToolbarLabel("网格");
				// 像素网格开关（放大后显示像素格子）
				if (gZoom >= 3.0f)
			{
				ImGui::Checkbox("像素网格", &g_ShowPixelGrid);
		}
		else
		{
			ImGui::BeginDisabled();
			bool dummy = false;
			ImGui::Checkbox("像素网格", &dummy);
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("放大到 3x 以上可用");
			}

				// 坐标网格：固定步长，跟随图片平移（参照 ImGui Demo Canvas 实现）
				ImGui::SameLine();
				ImGui::Checkbox("坐标网格", &g_ShowCoordGrid);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(80);
				ImGui::SliderInt("步长(px)", &g_GridStep, 1, 500);

			ImGui::PopStyleVar(2);
			ImGui::Separator();

		// ===== 视频/摄像头播放控制栏（仅当视频打开时显示）=====
		if (VideoCapture::IsOpen())
		{
			bool playing = VideoCapture::IsPlaying();
			const float btnW = 60.0f;

			// --- 播放/暂停按钮（绿/橙） ---
			{
				ImGui::PushStyleColor(ImGuiCol_Button, playing ? ImVec4(0.85f, 0.45f, 0.05f, 0.75f) : ImVec4(0.10f, 0.55f, 0.10f, 0.75f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, playing ? ImVec4(0.95f, 0.55f, 0.10f, 0.85f) : ImVec4(0.15f, 0.65f, 0.15f, 0.85f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, playing ? ImVec4(0.75f, 0.35f, 0.00f, 0.85f) : ImVec4(0.05f, 0.45f, 0.05f, 0.85f));
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
				if (ImGui::Button(playing ? " 暂停 " : " 播放 ", ImVec2(btnW, 0)))
					VideoCapture::TogglePlay();
				ImGui::PopStyleColor(4);
			}
			ImGui::SameLine();

			// --- 停止按钮（红） ---
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.18f, 0.18f, 0.75f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.25f, 0.25f, 0.85f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.60f, 0.12f, 0.12f, 0.85f));
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
				if (ImGui::Button(" 停止 ", ImVec2(btnW, 0)))
					VideoCapture::Stop();
				ImGui::PopStyleColor(4);
			}
			ImGui::SameLine();

			// --- 关闭按钮（灰） ---
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.35f, 0.35f, 0.75f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.45f, 0.45f, 0.85f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.30f, 0.30f, 0.85f));
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
				if (ImGui::Button(" 关闭 ", ImVec2(btnW, 0)))
					VideoCapture::Close();
				ImGui::PopStyleColor(4);
			}
			ImGui::SameLine();

			// --- 进度条 / 帧滑动条 ---
			if (!VideoCapture::IsCamera())
			{
				int total = VideoCapture::GetFrameCount();
				int cur = VideoCapture::GetCurrentFrame();
				if (total > 0)
				{
					ImGui::PushItemWidth(200);
					if (ImGui::SliderInt("##frameSlider", &cur, 0, total - 1, "%d"))
						VideoCapture::SeekFrame(cur);
					ImGui::PopItemWidth();
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("拖动跳转到指定帧");
					ImGui::SameLine();
				}
			}
			else
			{
				ImGui::Text(" 帧: %d", VideoCapture::GetCurrentFrame());
				ImGui::SameLine();
			}

			// --- 循环播放开关 ---
			bool loop = VideoCapture::IsLooping();
			if (ImGui::Checkbox("循环", &loop))
				VideoCapture::SetLoop(loop);
			ImGui::SameLine();

			// --- FPS 和状态指示 ---
			ImGui::TextColored(
				playing ? ImVec4(0.3f, 1.0f, 0.3f, 1) : ImVec4(0.7f, 0.7f, 0.7f, 1),
				"%.1f fps %s",
				VideoCapture::GetFPS(),
				VideoCapture::IsCamera() ? "[摄像头]" : "[视频]");

			ImGui::Separator();
		}

		// 为底部浏览工具栏预留空间
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_ChildBg));
			ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetStyleColorVec4(ImGuiCol_Border));
		ImGui::BeginChild("ImageRegion", ImVec2(0, -35.0f), true,
						  ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleColor(2);
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
				gZoom = std::clamp(gZoom, 0.005f, 50.0f); // 最小0.5%
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

			// 像素网格：fmodf 模式，跟随平移（参照 ImGui Demo）
			if (g_ShowPixelGrid && gZoom >= 3.0f)
			{
				ImDrawList *dl = ImGui::GetWindowDrawList();

				ImU32 fineColor = IM_COL32(0, 0, 0, 60);
				ImU32 majorColor = IM_COL32(0, 0, 0, 120);

				// 竖线：步长 = 1 像素 × 缩放
				float x0 = drawPos.x + fmodf(gPan.x, gZoom);
				int col = (int)((x0 - drawPos.x) / gZoom); // 起始列号
				for (float x = x0; x < drawPos.x + drawW; x += gZoom, col++)
				{
					if (x < drawPos.x)
						continue;
					bool isMajor = (col % 10 == 0);
					dl->AddLine(ImVec2(x, drawPos.y), ImVec2(x, drawPos.y + drawH),
								isMajor ? majorColor : fineColor,
								isMajor ? 1.0f : 0.5f);
				}

				// 横线
				float y0 = drawPos.y + fmodf(gPan.y, gZoom);
				int row = (int)((y0 - drawPos.y) / gZoom);
				for (float y = y0; y < drawPos.y + drawH; y += gZoom, row++)
				{
					if (y < drawPos.y)
						continue;
					bool isMajor = (row % 10 == 0);
					dl->AddLine(ImVec2(drawPos.x, y), ImVec2(drawPos.x + drawW, y),
								isMajor ? majorColor : fineColor,
								isMajor ? 1.0f : 0.5f);
				}
			}

			// 工具叠加层绘制（ImDrawList 零拷贝）
			auto DrawBoxOverlays = [](ImDrawList* dl, const std::vector<DetectedObject>& objs, ImU32 defaultColor)
			{
				static const ImU32 s_Colors[] = {
					IM_COL32(0, 255, 0, 255), IM_COL32(0, 165, 255, 255),
					IM_COL32(255, 0, 255, 255), IM_COL32(0, 255, 255, 255),
					IM_COL32(255, 255, 0, 255), IM_COL32(128, 0, 255, 255),
					IM_COL32(255, 0, 128, 255), IM_COL32(50, 205, 50, 255),
				};
				constexpr int nCol = sizeof(s_Colors) / sizeof(s_Colors[0]);
				for (const auto& o : objs)
				{
					ImU32 col = (o.classId >= 0) ? s_Colors[o.classId % nCol] : defaultColor;
					ImVec2 p1 = UI::ImageToScreenPos(ImVec2((float)o.box.x, (float)o.box.y));
					ImVec2 p2 = UI::ImageToScreenPos(ImVec2((float)(o.box.x + o.box.width), (float)(o.box.y + o.box.height)));
					float thick = (std::max)(2.0f, 3.0f * gZoom);
					dl->AddRect(p1, p2, col, 0.0f, 0, thick);
					if (!o.className.empty())
					{
						char lbl[128];
						snprintf(lbl, sizeof(lbl), "%s %.2f", o.className.c_str(), o.confidence);
						dl->AddText(ImVec2(p1.x + 2, p1.y - ImGui::GetFontSize() - 2), IM_COL32(255,255,255,255), lbl);
					}
				}
			};
			ImDrawList *dl = ImGui::GetWindowDrawList();

			// YOLO 实时检测叠加层（保留：有视频偏移补偿逻辑）
			if (g_YoloShowOverlay && !g_YoloOverlays.empty())
			{
				auto DrawBoxOverlays = [](ImDrawList* dl, const std::vector<DetectedObject>& objs, ImU32 defaultColor)
				{
					static const ImU32 s_Colors[] = {
						IM_COL32(0, 255, 0, 255), IM_COL32(0, 165, 255, 255),
						IM_COL32(255, 0, 255, 255), IM_COL32(0, 255, 255, 255),
						IM_COL32(255, 255, 0, 255), IM_COL32(128, 0, 255, 255),
						IM_COL32(255, 0, 128, 255), IM_COL32(50, 205, 50, 255),
					};
					constexpr int nCol = sizeof(s_Colors) / sizeof(s_Colors[0]);
					for (const auto& o : objs)
					{
						ImU32 col = (o.classId >= 0) ? s_Colors[o.classId % nCol] : defaultColor;
						ImVec2 p1 = UI::ImageToScreenPos(ImVec2((float)o.box.x, (float)o.box.y));
						ImVec2 p2 = UI::ImageToScreenPos(ImVec2((float)(o.box.x + o.box.width), (float)(o.box.y + o.box.height)));
						float thick = (std::max)(2.0f, 3.0f * gZoom);
						dl->AddRect(p1, p2, col, 0.0f, 0, thick);
						if (!o.className.empty())
						{
							char lbl[128];
							snprintf(lbl, sizeof(lbl), "%s %.2f", o.className.c_str(), o.confidence);
							dl->AddText(ImVec2(p1.x + 2, p1.y - ImGui::GetFontSize() - 2), IM_COL32(255,255,255,255), lbl);
						}
					}
				};
				auto offsetObjs = g_YoloOverlays;
				if (g_YoloOverlayOffsetX != 0.0f)
				{
					for (auto& o : offsetObjs)
						o.box.x += (int)g_YoloOverlayOffsetX;
				}
				DrawBoxOverlays(dl, offsetObjs, IM_COL32(0,255,0,255));
			}

			// 统一结果叠加层（Contour/Shape/Line/MCF 已全部迁移至此）
			DrawUnifiedResults(dl);

			// 坐标网格：固定步长，跟随平移（参照 ImGui Demo）
			if (g_ShowCoordGrid)
			{
				ImDrawList *dl = ImGui::GetWindowDrawList();
				float step = g_GridStep * gZoom;
				if (step < 8.0f)
					step = 8.0f;
				ImU32 gridColor = IM_COL32(255, 255, 255, 60);

				float x0 = drawPos.x + fmodf(gPan.x, step);
				for (float x = x0; x < drawPos.x + drawW; x += step)
					dl->AddLine(ImVec2(x, drawPos.y), ImVec2(x, drawPos.y + drawH), gridColor, 1.0f);

				float y0 = drawPos.y + fmodf(gPan.y, step);
				for (float y = y0; y < drawPos.y + drawH; y += step)
					dl->AddLine(ImVec2(drawPos.x, y), ImVec2(drawPos.x + drawW, y), gridColor, 1.0f);
			}
		}

		if (!gTexture || gImageWidth <= 0 || gImageHeight <= 0)
		{
			ImVec2 avail = ImGui::GetContentRegionAvail();
			ImVec2 textSize = ImGui::CalcTextSize("暂无图片");
			ImGui::SetCursorPos(ImVec2(
				(std::max)(0.0f, (avail.x - textSize.x) * 0.5f),
				(std::max)(0.0f, (avail.y - textSize.y) * 0.5f)));
			ImGui::TextDisabled("暂无图片");
		}

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

		// 首张按钮（回到第一张）
		bool hasFirst = (!gImageList.empty() && gCurrentImageIndex > 0);
		if (!hasFirst)
			ImGui::BeginDisabled();
		if (ImGui::Button("首张", ImVec2(buttonWidth, 0)))
			NavigateToImage(0);
		if (!hasFirst)
			ImGui::EndDisabled();
		ImGui::SameLine();

		// 上一张按钮（只有列表非空且有上一张时可用）
		bool hasPrev = (!gImageList.empty() && gCurrentImageIndex > 0);
		if (!hasPrev)
			ImGui::BeginDisabled();
		if (ImGui::Button("上一张", ImVec2(buttonWidth, 0)))
			NavigatePrevImage();
		if (!hasPrev)
			ImGui::EndDisabled();
		ImGui::SameLine();

		// 下一张按钮
		bool hasNext = (!gImageList.empty() && gCurrentImageIndex >= 0 &&
						gCurrentImageIndex < (int)gImageList.size() - 1);
		if (!hasNext)
			ImGui::BeginDisabled();
		if (ImGui::Button("下一张", ImVec2(buttonWidth, 0)))
			NavigateNextImage();
		if (!hasNext)
			ImGui::EndDisabled();
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
				// 清除所有工具叠加
				g_YoloShowOverlay = false;
				g_YoloOverlays.clear();
				gContext.ClearUnifiedResults();
			// 单独选择图片时清空列表模式
				gImageList.clear();
				gCurrentImageIndex = -1;
			}
			else
			{
				LogSystem::Add(LOG_WARN, color, "选择图片 - 用户取消了选择或路径为空");
			}
		}

		// ===== 右侧信息栏：尺寸 | 格式 | 像素坐标 | RGB值 =====
		{
			ImGui::SameLine();
			if (!gImage.empty())
			{
				const char *fmtStr = "?";
				int ch = gImage.channels();
				if (ch == 1)
					fmtStr = "Gray";
				else if (ch == 3)
					fmtStr = "BGR";
				else if (ch == 4)
					fmtStr = "BGRA";
				// 仅鼠标在图片窗口范围内才显示像素坐标
				ImVec2 mouse = ImGui::GetMousePos();
				bool inChild = (mouse.x >= childScreenPos.x && mouse.x < childScreenPos.x + childScreenSize.x &&
								mouse.y >= childScreenPos.y && mouse.y < childScreenPos.y + childScreenSize.y);
				if (inChild)
				{
					ImVec2 imgCoord = ScreenToImagePos(mouse);
					int ix = (int)imgCoord.x, iy = (int)imgCoord.y;
					bool inImg = (ix >= 0 && ix < gImageWidth && iy >= 0 && iy < gImageHeight);
					if (inImg)
					{
						// 读取像素值
						char pixInfo[64] = "";
						if (ch == 1)
						{
							uchar v = gImage.at<uchar>(iy, ix);
							snprintf(pixInfo, sizeof(pixInfo), " | Gray:%d", v);
						}
						else if (ch == 3)
						{
							cv::Vec3b bgr = gImage.at<cv::Vec3b>(iy, ix);
							snprintf(pixInfo, sizeof(pixInfo), " | R:%d G:%d B:%d", bgr[2], bgr[1], bgr[0]);
						}
						else if (ch == 4)
						{
							cv::Vec4b bgra = gImage.at<cv::Vec4b>(iy, ix);
							snprintf(pixInfo, sizeof(pixInfo), " | R:%d G:%d B:%d A:%d", bgra[2], bgra[1], bgra[0], bgra[3]);
						}
						ImGui::TextDisabled("%dx%d %s | X:%.0f Y:%.0f%s",
											gImageWidth, gImageHeight, fmtStr, imgCoord.x, imgCoord.y, pixInfo);
					}
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
		if (gImageWidth <= 0 || gImageHeight <= 0)
			return;

		float regionW = gCanvasSize.x;
		float regionH = gCanvasSize.y;
		float scaleX = regionW / (float)gImageWidth;
		float scaleY = regionH / (float)gImageHeight;
		gZoom = (scaleX < scaleY) ? scaleX : scaleY;
		if (gZoom > 1.0f)
			gZoom = 1.0f;
		float drawW = gImageWidth * gZoom;
		float drawH = gImageHeight * gZoom;
		gPan.x = (regionW - drawW) * 0.5f;
		gPan.y = (regionH - drawH) * 0.5f;
	}

	void ClearImage()
	{
		// ⭐ 先停掉所有依赖图像的运行时
		UI::g_YoloLiveDetect = false;
		VideoCapture::Close();

		// ⭐ 清除图像数据（否则 !gImage.empty() 仍为 true，检测继续跑）
		gImage = cv::Mat();
		gOriginalImage = cv::Mat();
		gPendingUpload = cv::Mat();
		gNeedUpload = false;

			gImageWidth = 0;
			gImageHeight = 0;
			FrameSourceState::Clear();
			ClearROIState();
		TemplateMatch::Clear();
		g_YoloShowOverlay = false;
		g_YoloOverlays.clear();
		gContext.ClearUnifiedResults();
		gZoom = 1.0f;
		gPan = ImVec2(0, 0);
		imageScreenPos = ImVec2(0, 0);
	}

	// =====================================================
	// 从文件夹加载所有图片
	// =====================================================
	void LoadFolderImages(const std::string &folderPath)
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

		// 清除上一次的 ROI、模板匹配和所有工具叠加
		ClearROIState();
		TemplateMatch::Clear();
		g_YoloShowOverlay = false;
		g_YoloOverlays.clear();
			gContext.ClearUnifiedResults();

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
