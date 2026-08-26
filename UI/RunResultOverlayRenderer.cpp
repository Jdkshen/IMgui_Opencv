#include "RunResultOverlayRenderer.h"

#include "RunResultLayout.h"
#include "../Core/ResultOverlayState.h"
#include "../Core/ToolResultUtils.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>

namespace UI::RunResultOverlayRenderer
{
    using RunResultSnapshotModel::RunResultSnapshot;

    ImU32 ResultOverlayColor(const ToolResult& result, std::size_t index)
    {
        if (result.status == ToolResultStatus::Fail)
            return IM_COL32(255, 70, 62, 255);
        if (result.status == ToolResultStatus::Error)
            return IM_COL32(255, 166, 45, 255);
        static constexpr ImU32 colors[] = {
            IM_COL32(35, 230, 105, 255),
            IM_COL32(35, 190, 255, 255),
            IM_COL32(255, 190, 35, 255),
            IM_COL32(210, 90, 255, 255),
            IM_COL32(25, 230, 215, 255),
        };
        return colors[index % IM_ARRAYSIZE(colors)];
    }

    std::string TruncateOverlayLabel(const char* text, std::size_t maxBytes = 52)
    {
        std::string value = text ? text : "";
        if (value.size() <= maxBytes)
            return value;
        std::size_t cut = maxBytes;
        while (cut > 0 &&
            (static_cast<unsigned char>(value[cut]) & 0xC0) == 0x80)
        {
            --cut;
        }
        return value.substr(0, cut) + "...";
    }

    void DrawOverlayLabel(ImDrawList* drawList, const ImVec2& anchor,
        const char* text, ImU32 color, const ImVec2& imageMin, const ImVec2& imageMax,
        std::vector<RunResultLayout::Rect>& occupiedLabels)
    {
        if (!text || !*text)
            return;

        const std::string displayText = TruncateOverlayLabel(text);
        const ImVec2 textSize = ImGui::CalcTextSize(displayText.c_str());
        const RunResultLayout::LabelPlacement placement =
            RunResultLayout::PlaceOverlayLabel(
                {anchor.x, anchor.y}, {textSize.x, textSize.y},
                {imageMin.x, imageMin.y, imageMax.x, imageMax.y},
                occupiedLabels);
        if (!placement.placed)
            return;

        occupiedLabels.push_back(placement.bounds);
        const ImVec2 position(
            placement.textPosition.x, placement.textPosition.y);
        const ImVec2 backgroundMin(
            placement.bounds.left, placement.bounds.top);
        const ImVec2 backgroundMax(
            placement.bounds.right, placement.bounds.bottom);
        drawList->AddRectFilled(backgroundMin, backgroundMax, IM_COL32(15, 18, 22, 225), 3.0f);
        drawList->AddRect(backgroundMin, backgroundMax, color, 3.0f, 0, 1.0f);
        drawList->AddText(position, IM_COL32(255, 255, 255, 255), displayText.c_str());
    }

    void DrawResultImageOverlays(const RunResultSnapshot& snapshot,
        const ImVec2& imageMin, const ImVec2& imageMax, float scale,
        bool allowLabels)
    {
        if (snapshot.overlayResults.empty())
            return;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float thickness = (std::clamp)(scale * 2.0f, 1.5f, 4.0f);
        auto ToScreen = [&imageMin, scale](float x, float y)
        {
            return ImVec2(imageMin.x + x * scale, imageMin.y + y * scale);
        };

        int labelCount = 0;
        const int maximumLabels = ResultOverlayState::MaxVisibleLabels();
        std::vector<RunResultLayout::Rect> occupiedLabels;
        std::unordered_set<std::string> drawnLabelTexts;
        auto CanDrawLabel = [&labelCount, maximumLabels, &drawnLabelTexts](const std::string& text)
        {
            if (labelCount >= maximumLabels || !drawnLabelTexts.insert(text).second)
                return false;
            ++labelCount;
            return true;
        };
        drawList->PushClipRect(imageMin, imageMax, true);
        for (std::size_t resultIndex = 0;
            resultIndex < snapshot.overlayResults.size(); ++resultIndex)
        {
            const ToolResult& result = snapshot.overlayResults[resultIndex];
            if (result.skipped)
                continue;
            const ImU32 color = ResultOverlayColor(result, resultIndex);
            const bool showLabels = allowLabels &&
                ResultOverlayState::ShouldDrawResultLabels(result) &&
                maximumLabels > 0;
            const bool showDetectionLabels = showLabels && result.texts.empty();
            const bool showRegionLabels = showLabels && result.texts.empty() &&
                result.detections.empty();

            for (const ToolResult::Detection& detection : result.detections)
            {
                const ImVec2 boxMin = ToScreen(
                    static_cast<float>(detection.box.x), static_cast<float>(detection.box.y));
                const ImVec2 boxMax = ToScreen(
                    static_cast<float>(detection.box.x + detection.box.width),
                    static_cast<float>(detection.box.y + detection.box.height));
                drawList->AddRect(boxMin, boxMax, color, 2.0f, 0, thickness);
                if (showDetectionLabels)
                {
                    char label[192]{};
                    const char* name = detection.label.empty()
                        ? result.toolName.c_str() : detection.label.c_str();
                    const std::string displayName =
                        ResultOverlayState::BuildLabel(result, name);
                    if (detection.score > 0.0f)
                        std::snprintf(label, sizeof(label), "%s  %.2f",
                            displayName.c_str(), detection.score);
                    else
                        std::snprintf(label, sizeof(label), "%s", displayName.c_str());
                    if (CanDrawLabel(label))
                        DrawOverlayLabel(drawList, boxMin, label, color,
                            imageMin, imageMax, occupiedLabels);
                }
            }

            for (const ToolResult::Region& region : result.regions)
            {
                const ImVec2 boxMin = ToScreen(
                    static_cast<float>(region.bbox.x), static_cast<float>(region.bbox.y));
                const ImVec2 boxMax = ToScreen(
                    static_cast<float>(region.bbox.x + region.bbox.width),
                    static_cast<float>(region.bbox.y + region.bbox.height));
                if (region.bbox.width > 0 && region.bbox.height > 0)
                    drawList->AddRect(boxMin, boxMax, color, 2.0f, 0, thickness);
                if (region.contour.size() >= 2)
                {
                    std::vector<ImVec2> contour;
                    contour.reserve(region.contour.size());
                    for (const cv::Point& point : region.contour)
                        contour.push_back(ToScreen(static_cast<float>(point.x), static_cast<float>(point.y)));
                    drawList->AddPolyline(contour.data(), static_cast<int>(contour.size()),
                        color, ImDrawFlags_Closed, thickness);
                }
                // Keep the geometry from every result type, while assigning labels
                // to the most informative type only: text, detection, then region.
                if (showRegionLabels &&
                    ResultOverlayState::ShouldDrawRegionLabel(result, region.label) &&
                    CanDrawLabel(region.label.empty()
                        ? result.toolName : region.label))
                {
                    char label[192]{};
                    const char* name = region.label.empty()
                        ? result.toolName.c_str() : region.label.c_str();
                    const std::string displayName =
                        ResultOverlayState::BuildLabel(result, name);
                    if (region.score > 0.0f)
                        std::snprintf(label, sizeof(label), "%s  %.2f",
                            displayName.c_str(), region.score);
                    else
                        std::snprintf(label, sizeof(label), "%s", displayName.c_str());
                    DrawOverlayLabel(drawList, boxMin, label, color,
                        imageMin, imageMax, occupiedLabels);
                }
            }

            for (const ToolResult::TextItem& text : result.texts)
            {
                const ImVec2 boxMin = ToScreen(
                    static_cast<float>(text.box.x), static_cast<float>(text.box.y));
                const ImVec2 boxMax = ToScreen(
                    static_cast<float>(text.box.x + text.box.width),
                    static_cast<float>(text.box.y + text.box.height));
                drawList->AddRect(boxMin, boxMax, color, 2.0f, 0, thickness);
                if (showLabels)
                {
                    char label[256]{};
                    const char* recognized = text.text.empty()
                        ? result.toolName.c_str() : text.text.c_str();
                    const std::string displayText =
                        ResultOverlayState::BuildLabel(result, recognized);
                    if (text.confidence > 0.0f)
                        std::snprintf(label, sizeof(label), "%s  %.2f",
                            displayText.c_str(), text.confidence);
                    else
                        std::snprintf(label, sizeof(label), "%s", displayText.c_str());
                    if (CanDrawLabel(label))
                        DrawOverlayLabel(drawList, boxMin, label, color,
                            imageMin, imageMax, occupiedLabels);
                }
            }

            bool lineLabelDrawn = !result.detections.empty() ||
                !result.regions.empty() || !result.texts.empty();
            for (const ToolResult::Line& line : result.lines)
            {
                const ImVec2 p1 = ToScreen(static_cast<float>(line.p1.x), static_cast<float>(line.p1.y));
                const ImVec2 p2 = ToScreen(static_cast<float>(line.p2.x), static_cast<float>(line.p2.y));
                drawList->AddLine(p1, p2, color, thickness);
                if (showLabels && !lineLabelDrawn)
                {
                    const std::string lineLabel =
                        BuildToolResultLineOverlayLabel(result);
                    if (CanDrawLabel(lineLabel))
                    {
                        DrawOverlayLabel(drawList, p1, lineLabel.c_str(),
                            color, imageMin, imageMax, occupiedLabels);
                        lineLabelDrawn = true;
                    }
                }
            }
        }
        drawList->PopClipRect();
    }
}
