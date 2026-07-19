#include "../framework.h"
#include "../imgui/imgui.h"
#include "../imgui/imgui_impl_dx12.h"
#include "ImageViewer.h"
#include "ROIManager.h"
#include "../Core/OpenFileDialog.h"
#include "../Core/DX12Context.h"
#include "../Core/FrameNavigation.h"
#include "../Core/ImageState.h"
#include "../Core/ImageViewState.h"
#include "../Core/ImageLoadController.h"
#include "../Core/ImageImportService.h"
#include "../Core/ResultOverlayState.h"
#include "../Core/ThemeManager.h"
#include "../Algorithm/ITool.h"
#include "../Log/LogSystem.h"

#include <cmath>
#include <utility>

extern bool g_ShowOpenCV;

namespace
{
const ImVec4 kImageLogColor(0.2f, 0.8f, 1.0f, 1.0f);
std::string s_ImageImportError;
bool s_OpenImageImportError = false;

void ReportImageImportError(std::string message)
{
    s_ImageImportError = std::move(message);
    s_OpenImageImportError = true;
}
}

namespace
{
// Keep the rendering code readable while keeping ownership in Core.
float& gZoom = ImageViewState::Zoom();
ImVec2& gPan = ImageViewState::Pan();
ImVec2& gCanvasSize = ImageViewState::CanvasSize();
ImVec2& imageScreenPos = ImageViewState::ImageScreenPos();
bool& g_ShowPixelGrid = ImageViewState::ShowPixelGrid();
bool& g_ShowCoordGrid = ImageViewState::ShowCoordGrid();
int& g_GridStep = ImageViewState::GridStep();
}

// 旧叠加层：仅保留 YOLO（有视频偏移补偿逻辑）和统一结果

// ---- 统一结果叠加层（唯一结果源） ----
static std::string TruncateUtf8Label(const std::string& text, size_t maxBytes)
{
    if (text.size() <= maxBytes) return text;
    size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
        --cut;
    if (cut == 0) cut = maxBytes;
    return text.substr(0, cut) + "...";
}

struct OverlayLabelRect
{
    ImVec2 min;
    ImVec2 max;
};

struct OverlayLabelState
{
    std::vector<OverlayLabelRect> occupied;
    int drawnCount = 0;
};

static bool RectsOverlap(const OverlayLabelRect& a, const OverlayLabelRect& b)
{
    return a.min.x < b.max.x && a.max.x > b.min.x &&
        a.min.y < b.max.y && a.max.y > b.min.y;
}

static bool IsFinitePoint(ImVec2 p)
{
    return std::isfinite(p.x) && std::isfinite(p.y);
}

static bool DrawReadableLabel(ImDrawList* dl, ImVec2 anchor, ImU32 accent, const char* label, OverlayLabelState& labelState)
{
    if (!label || !label[0])
        return false;
    const int maxLabels = ResultOverlayState::MaxVisibleLabels();
    if (maxLabels <= 0 || labelState.drawnCount >= maxLabels || !IsFinitePoint(anchor))
        return false;

    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const float padX = 4.0f;
    const float padY = 2.0f;
    const float fontH = ImGui::GetFontSize();
    const float topLimit = ImGui::GetWindowPos().y + 2.0f;
    const float rowH = textSize.y + padY * 2.0f + 3.0f;

    ImVec2 pos(anchor.x + 2.0f, anchor.y - fontH - padY * 2.0f - 2.0f);
    if (pos.y < topLimit)
        pos.y = anchor.y + 2.0f;

    OverlayLabelRect rect{};
    bool placed = false;
    const int attempts = ResultOverlayState::ReadOnlySettings().avoidLabelOverlap ? 8 : 1;
    for (int i = 0; i < attempts; ++i)
    {
        ImVec2 candidate = ImVec2(pos.x, pos.y + rowH * i);
        rect.min = ImVec2(candidate.x - padX, candidate.y - padY);
        rect.max = ImVec2(candidate.x + textSize.x + padX, candidate.y + textSize.y + padY);

        bool overlaps = false;
        if (ResultOverlayState::ReadOnlySettings().avoidLabelOverlap)
        {
            for (const auto& used : labelState.occupied)
            {
                if (RectsOverlap(rect, used))
                {
                    overlaps = true;
                    break;
                }
            }
        }
        if (!overlaps)
        {
            pos = candidate;
            placed = true;
            break;
        }
    }
    if (!placed)
        return false;

    ImVec2 bgMin = rect.min;
    ImVec2 bgMax = rect.max;
    dl->AddRectFilled(bgMin, bgMax, IM_COL32(18, 22, 28, 230), 3.0f);
    dl->AddRect(bgMin, bgMax, accent, 3.0f, 0, 1.0f);
    dl->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f), IM_COL32(0, 0, 0, 220), label);
    dl->AddText(pos, IM_COL32(255, 235, 120, 255), label);
    labelState.occupied.push_back(rect);
    ++labelState.drawnCount;
    return true;
}

static void DrawUnifiedResults(ImDrawList* dl)
{
    const auto& results = ResultOverlayState::Results();
    if (results.empty()) return;
    static const ImU32 cols[] = {
        IM_COL32(0,255,0,255), IM_COL32(255,128,0,255), IM_COL32(0,128,255,255),
        IM_COL32(255,0,255,255), IM_COL32(0,255,255,255),
    };
    constexpr int nCol = 5;
    OverlayLabelState labelState;
    for (size_t i = 0; i < results.size(); i++)
    {
        const auto& r = results[i];
        if (!r.success) continue;
        const bool drawLabels = ResultOverlayState::ShouldDrawResultLabels(r);
        float t = std::max(2.0f, 3.0f * gZoom);

        // 检测框
        for (const auto& d : r.detections) {
            auto p1 = UI::ImageToScreenPos(ImVec2((float)d.box.x, (float)d.box.y));
            auto p2 = UI::ImageToScreenPos(ImVec2((float)(d.box.x+d.box.width), (float)(d.box.y+d.box.height)));
            dl->AddRect(p1, p2, cols[i % nCol], 0, 0, t);
            if (drawLabels && !d.label.empty()) {
                const std::string label = ResultOverlayState::BuildLabel(r, d.label);
                char buf[160]; snprintf(buf, sizeof(buf), "%s %.2f", label.c_str(), d.score);
                DrawReadableLabel(dl, p1, cols[i % nCol], buf, labelState);
            }
        }

        // 区域（轮廓多边形 + 顶点圆点）
        if (r.texts.empty()) for (const auto& reg : r.regions) {
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
                if (ResultOverlayState::ShouldDrawRegionLabel(r, reg.label)) {
                    const std::string label = ResultOverlayState::BuildLabel(r, reg.label);
                    DrawReadableLabel(dl, p1, rectColor, label.c_str(), labelState);
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
        bool lineLabelDrawn = false;
        for (const auto& l : r.lines) {
            auto p1 = UI::ImageToScreenPos(ImVec2((float)l.p1.x, (float)l.p1.y));
            auto p2 = UI::ImageToScreenPos(ImVec2((float)l.p2.x, (float)l.p2.y));
            dl->AddLine(p1, p2, cols[i % nCol], t);
            if (drawLabels && !lineLabelDrawn)
            {
                std::string label = r.toolName;
                if (!r.message.empty() && label.find(r.message) == std::string::npos)
                    label += " " + r.message;
                DrawReadableLabel(dl, p1, cols[i % nCol], label.c_str(), labelState);
                lineLabelDrawn = true;
            }
        }

	        // OCR 文本
	        for (const auto& text : r.texts) {
	            auto p1 = UI::ImageToScreenPos(ImVec2((float)text.box.x, (float)text.box.y));
	            auto p2 = UI::ImageToScreenPos(ImVec2((float)(text.box.x + text.box.width), (float)(text.box.y + text.box.height)));
	            ImU32 boxColor = IM_COL32(0, 220, 120, 255);
	            dl->AddRect(p1, p2, boxColor, 0, 0, t);
		            if (drawLabels && !text.text.empty()) {
		                std::string preview = TruncateUtf8Label(text.text, 48);
		                char buf[192];
		                snprintf(buf, sizeof(buf), "%s %.2f", preview.c_str(), text.confidence);
		                DrawReadableLabel(dl, p1, boxColor, buf, labelState);
		            }
	        }
    }
}

static void DrawFixtureOverlays(ImDrawList* dl)
{
    const auto overlays = ResultOverlayState::FixtureOverlays();
    OverlayLabelState labelState;
    constexpr float axisLength = 36.0f;
    for (const auto& overlay : overlays)
    {
        const float referenceAngle = overlay.referenceAngleDegrees * 3.14159265f / 180.0f;
        const float currentAngle = overlay.currentAngleDegrees * 3.14159265f / 180.0f;
        const auto drawAxis = [&](cv::Point2f origin, float angle, ImU32 xColor, ImU32 yColor,
                                  const char* label)
        {
            const ImVec2 center = UI::ImageToScreenPos(ImVec2(origin.x, origin.y));
            const ImVec2 xEnd = UI::ImageToScreenPos(ImVec2(
                origin.x + std::cos(angle) * axisLength,
                origin.y + std::sin(angle) * axisLength));
            const ImVec2 yEnd = UI::ImageToScreenPos(ImVec2(
                origin.x - std::sin(angle) * axisLength,
                origin.y + std::cos(angle) * axisLength));
            dl->AddCircleFilled(center, std::max(3.0f, 4.0f * gZoom), IM_COL32(255, 255, 255, 220));
            dl->AddLine(center, xEnd, xColor, std::max(1.5f, 2.0f * gZoom));
            dl->AddLine(center, yEnd, yColor, std::max(1.5f, 2.0f * gZoom));
            if (overlay.showLabel)
                DrawReadableLabel(dl, center, xColor, label, labelState);
        };

        drawAxis(overlay.referenceOrigin, referenceAngle,
            IM_COL32(255, 180, 0, 230), IM_COL32(255, 100, 0, 230), "Fixture参考");
        drawAxis(overlay.currentOrigin, currentAngle,
            IM_COL32(0, 220, 255, 240), IM_COL32(0, 150, 255, 240), "Fixture当前");
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
			const bool isDark = g_CurrentTheme == 0;
			if (FrameNavigation::ConsumeFitRequest())
				FitImageToWindow();

			std::string asyncLoadError;
			if (ImageLoadController::ConsumeLastError(asyncLoadError))
				ReportImageImportError(std::move(asyncLoadError));
			if (s_OpenImageImportError)
			{
				ImGui::OpenPopup("图片导入失败");
				s_OpenImageImportError = false;
			}
				if (ImGui::IsPopupOpen("图片导入失败"))
				{
					ImGui::SetNextWindowSizeConstraints(ImVec2(420.0f, 0.0f), ImVec2(650.0f, FLT_MAX));
					if (ImGui::BeginPopupModal("图片导入失败", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
					{
						ImGui::TextWrapped("%s", s_ImageImportError.c_str());
						ImGui::Spacing();
						if (ImGui::Button("确定", ImVec2(100.0f, 0.0f)))
							ImGui::CloseCurrentPopup();
						ImGui::EndPopup();
					}
				}

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
						ClearROIState();
						gDrawingROI = false;
						ResultOverlayState::ClearResults();
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
							FrameNavigation::OpenVideoSource(path);
					}
				}
				ImGui::SameLine();
				// 打开摄像头
				if (ImGui::Button("打开摄像头"))
				{
						FrameNavigation::OpenCameraSource(0);
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
					ImGui::SameLine();
					ToolbarLabel("结果");
					auto& overlaySettings = ResultOverlayState::MutableSettings();
					ImGui::Checkbox("标签##result_overlay_labels", &overlaySettings.showLabels);
					ImGui::SameLine();
					ImGui::Checkbox("避让##result_overlay_avoid", &overlaySettings.avoidLabelOverlap);
					ImGui::SameLine();
					ImGui::SetNextItemWidth(90);
					ImGui::SliderInt("最大标签##result_overlay_max", &overlaySettings.maxVisibleLabels, 0, 200);

				ImGui::PopStyleVar(2);
			ImGui::Separator();

		// ===== 视频/摄像头播放控制栏（仅当视频打开时显示）=====
		const FrameNavigation::PlaybackState playback = FrameNavigation::CurrentPlayback();
		if (playback.open)
		{
			const bool playing = playback.playing;
			const float btnW = 60.0f;

			// --- 播放/暂停按钮（绿/橙） ---
			{
				ImGui::PushStyleColor(ImGuiCol_Button, playing ? ImVec4(0.85f, 0.45f, 0.05f, 0.75f) : ImVec4(0.10f, 0.55f, 0.10f, 0.75f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, playing ? ImVec4(0.95f, 0.55f, 0.10f, 0.85f) : ImVec4(0.15f, 0.65f, 0.15f, 0.85f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, playing ? ImVec4(0.75f, 0.35f, 0.00f, 0.85f) : ImVec4(0.05f, 0.45f, 0.05f, 0.85f));
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
				if (ImGui::Button(playing ? " 暂停 " : " 播放 ", ImVec2(btnW, 0)))
					FrameNavigation::TogglePlayback();
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
					FrameNavigation::StopPlayback();
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
					FrameNavigation::ClosePlayback();
				ImGui::PopStyleColor(4);
			}
			ImGui::SameLine();

			// --- 进度条 / 帧滑动条 ---
			if (!playback.camera)
			{
				const int total = playback.frameCount;
				int cur = playback.currentFrame;
				if (total > 0)
				{
					ImGui::PushItemWidth(200);
					if (ImGui::SliderInt("##frameSlider", &cur, 0, total - 1, "%d"))
						FrameNavigation::SeekPlaybackFrame(cur);
					ImGui::PopItemWidth();
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("拖动跳转到指定帧");
					ImGui::SameLine();
				}
			}
			else
			{
				ImGui::Text(" 帧: %d", playback.currentFrame);
				ImGui::SameLine();
			}

			// --- 循环播放开关 ---
			bool loop = playback.looping;
			if (ImGui::Checkbox("循环", &loop))
				FrameNavigation::SetPlaybackLoop(loop);
			ImGui::SameLine();

			// --- FPS 和状态指示 ---
			ImGui::TextColored(
				playing
					? (isDark ? ImVec4(0.34f, 0.78f, 0.48f, 1.0f) : ImVec4(0.05f, 0.40f, 0.19f, 1.0f))
					: ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
				"%.1f fps %s",
				playback.fps,
				playback.camera ? "[摄像头]" : "[视频]");

			ImGui::Separator();
		}

		// 为底部浏览工具栏预留空间
			ImGui::PushStyleColor(ImGuiCol_ChildBg, isDark
				? ImVec4(0.040f, 0.047f, 0.055f, 1.0f)
				: ImVec4(0.76f, 0.79f, 0.81f, 1.0f));
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
			float drawW = ImageState::Width() * gZoom;
			float drawH = ImageState::Height() * gZoom;
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
			if (ResultOverlayState::IsRealtimeOverlayVisible() &&
				!ResultOverlayState::RealtimeObjects().empty())
			{
				auto offsetObjs = ResultOverlayState::RealtimeObjects();
				const float offsetX = ResultOverlayState::RealtimeOverlayOffsetX();
				if (offsetX != 0.0f)
				{
					for (auto& o : offsetObjs)
						o.box.x += static_cast<int>(offsetX);
				}
				DrawBoxOverlays(dl, offsetObjs, IM_COL32(0,255,0,255));
			}

			// 统一结果叠加层（Contour/Shape/Line/MCF 已全部迁移至此）
			DrawUnifiedResults(dl);
			DrawFixtureOverlays(dl);

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

		if (!gTexture || ImageState::Width() <= 0 || ImageState::Height() <= 0)
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
					const ImageImportResult result = ImageImportService::ImportFolder(folderPath);
					if (result.success)
						LogSystem::Add(LOG_INFO, kImageLogColor, "加载文件夹: %s, 共 %zu 张图片",
							folderPath.c_str(), result.imageCount);
					else
					{
						LogSystem::Add(LOG_WARN, kImageLogColor, "%s", result.message.c_str());
						ReportImageImportError(result.message);
					}
				}
			}
		ImGui::SameLine();

		// 首张按钮（回到第一张）
		bool hasFirst = (!FrameNavigation::ImageList().empty() && FrameNavigation::CurrentImageIndex() > 0);
		if (!hasFirst)
			ImGui::BeginDisabled();
			if (ImGui::Button("首张", ImVec2(buttonWidth, 0)))
				ImageImportService::NavigateToImage(0);
		if (!hasFirst)
			ImGui::EndDisabled();
		ImGui::SameLine();

		// 上一张按钮（只有列表非空且有上一张时可用）
		bool hasPrev = (!FrameNavigation::ImageList().empty() && FrameNavigation::CurrentImageIndex() > 0);
		if (!hasPrev)
			ImGui::BeginDisabled();
			if (ImGui::Button("上一张", ImVec2(buttonWidth, 0)))
				ImageImportService::NavigatePreviousImage();
		if (!hasPrev)
			ImGui::EndDisabled();
		ImGui::SameLine();

		// 下一张按钮
		bool hasNext = (!FrameNavigation::ImageList().empty() && FrameNavigation::CurrentImageIndex() >= 0 &&
						FrameNavigation::CurrentImageIndex() < (int)FrameNavigation::ImageList().size() - 1);
		if (!hasNext)
			ImGui::BeginDisabled();
			if (ImGui::Button("下一张", ImVec2(buttonWidth, 0)))
				ImageImportService::NavigateNextImage();
		if (!hasNext)
			ImGui::EndDisabled();
		ImGui::SameLine();

		// 图片计数显示
		if (!FrameNavigation::ImageList().empty() && FrameNavigation::CurrentImageIndex() >= 0)
		{
			ImGui::Text(" %d / %zu ", FrameNavigation::CurrentImageIndex() + 1, FrameNavigation::ImageList().size());
		}
		else
		{
			ImGui::Text(" 无列表 ");
		}
		ImGui::SameLine();

		if (ImGui::Button("选择图片", ImVec2(buttonWidth, 0)))
		{
			std::string selectedPath = OpenFileDialog();
			if (!selectedPath.empty())
			{
					const ImageImportResult result = ImageImportService::ImportSingleImage(selectedPath);
					if (result.success)
						LogSystem::Add(LOG_INFO, kImageLogColor, "选择图片路径: %s", selectedPath.c_str());
					else
					{
						LogSystem::Add(LOG_ERROR, kImageLogColor, "%s", result.message.c_str());
						ReportImageImportError(result.message);
					}
			}
			else
			{
				LogSystem::Add(LOG_WARN, kImageLogColor, "选择图片 - 用户取消了选择或路径为空");
			}
		}

		// ===== 右侧信息栏：尺寸 | 格式 | 像素坐标 | RGB值 =====
		{
			ImGui::SameLine();
			const cv::Mat image = ImageState::Current();
			if (!image.empty() && image.data)
			{
				const char *fmtStr = "?";
				const int ch = image.channels();
				const bool readable8u = (image.depth() == CV_8U) && (ch == 1 || ch == 3 || ch == 4);
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
					bool inImg = (ix >= 0 && ix < image.cols && iy >= 0 && iy < image.rows);
					if (inImg && readable8u)
					{
						// 读取像素值
						char pixInfo[64] = "";
						const uchar* row = image.ptr<uchar>(iy);
						if (ch == 1)
						{
							uchar v = row[ix];
							snprintf(pixInfo, sizeof(pixInfo), " | Gray:%d", v);
						}
						else if (ch == 3)
						{
							const uchar* bgr = row + ix * 3;
							snprintf(pixInfo, sizeof(pixInfo), " | R:%d G:%d B:%d", bgr[2], bgr[1], bgr[0]);
						}
						else if (ch == 4)
						{
							const uchar* bgra = row + ix * 4;
							snprintf(pixInfo, sizeof(pixInfo), " | R:%d G:%d B:%d A:%d", bgra[2], bgra[1], bgra[0], bgra[3]);
						}
							ImGui::TextDisabled("%dx%d %s | X:%.4f Y:%.4f%s",
												image.cols, image.rows, fmtStr, imgCoord.x, imgCoord.y, pixInfo);
						}
						else if (inImg)
							ImGui::TextDisabled("%dx%d %s | X:%.4f Y:%.4f",
												image.cols, image.rows, fmtStr, imgCoord.x, imgCoord.y);
					else
						ImGui::TextDisabled("%dx%d %s | X:--- Y:---",
											image.cols, image.rows, fmtStr);
				}
				else
				{
					ImGui::TextDisabled("%dx%d %s", image.cols, image.rows, fmtStr);
				}
			}
		}

		ImGui::End();
	}

	void FitImageToWindow()
	{
		if (ImageState::Width() <= 0 || ImageState::Height() <= 0)
			return;

		float regionW = gCanvasSize.x;
		float regionH = gCanvasSize.y;
		float scaleX = regionW / (float)ImageState::Width();
		float scaleY = regionH / (float)ImageState::Height();
		gZoom = (scaleX < scaleY) ? scaleX : scaleY;
		if (gZoom > 1.0f)
			gZoom = 1.0f;
		float drawW = ImageState::Width() * gZoom;
		float drawH = ImageState::Height() * gZoom;
		gPan.x = (regionW - drawW) * 0.5f;
		gPan.y = (regionH - drawH) * 0.5f;
	}

		void ClearImage()
		{
			ImageImportService::ClearCurrentInput();
			ClearROIState();
			gZoom = 1.0f;
		gPan = ImVec2(0, 0);
		imageScreenPos = ImVec2(0, 0);
	}

	// =====================================================
	// 从文件夹加载所有图片
	// =====================================================
	void LoadFolderImages(const std::string &folderPath)
	{
		const ImageImportResult result = ImageImportService::ImportFolder(folderPath);
		if (!result.success)
			ReportImageImportError(result.message);
	}

	// =====================================================
	// 切换到指定索引的图片
	// =====================================================
	void NavigateToImage(int index)
	{
		const ImageImportResult result = ImageImportService::NavigateToImage(index);
		if (result.success)
			LogSystem::Add(LOG_INFO, kImageLogColor, "切换到图片 [%d/%zu]: %s",
				result.imageIndex + 1, result.imageCount, result.imagePath.c_str());
	}

	// =====================================================
	// 上一张
	// =====================================================
	void NavigatePrevImage()
	{
		ImageImportService::NavigatePreviousImage();
	}

	// =====================================================
	// 下一张
	// =====================================================
	void NavigateNextImage()
	{
		ImageImportService::NavigateNextImage();
	}

}
