#include "GeometryDrawEditor.h"

#include "ROIManager.h"
#include "../Core/ImageState.h"
#include "../Core/ImageViewState.h"
#include "../Core/ToolChainState.h"
#include "../Core/ToolInstance.h"
#include "../include/imgui/imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <opencv2/geometry/2d.hpp>

namespace UI::GeometryDrawEditor
{
namespace
{
constexpr float kHandleRadius = 6.0f;
constexpr float kHitDistance = 9.0f;
constexpr int kRotatedCornerBase = 100;

enum class DragMode { None, Create, Point, Move, Rotate };

struct EditorState
{
    int toolIndex = -1;
    int selectedIndex = -1;
    GeometryPrimitiveType createType = GeometryPrimitiveType::Line;
    bool creating = false;
    bool hasDraft = false;
    GeometryPrimitive draft;
    DragMode dragMode = DragMode::None;
    int dragPoint = -1;
    cv::Point2f lastMouse;
    bool changed = false;
};

EditorState& State()
{
    static EditorState state;
    return state;
}

float CanvasZoom()
{
    return ImageViewState::Zoom();
}

ToolInstance* ToolAt(int index)
{
    ToolInstance* tool = ToolChainState::At(index);
    return tool && tool->type == 17 ? tool : nullptr;
}

ToolInstance* ActiveTool()
{
    EditorState& state = State();
    if (ToolChainState::ActiveIndex() != state.toolIndex)
        return nullptr;
    return ToolAt(state.toolIndex);
}

cv::Point2f ToImage(const ImVec2& screen)
{
    const ImVec2 point = ScreenToImagePos(screen);
    return cv::Point2f(point.x, point.y);
}

ImVec2 ToScreen(const cv::Point2f& image)
{
    return ImageToScreenPos(ImVec2(image.x, image.y));
}

cv::Point2f ClampToImage(cv::Point2f point)
{
    point.x = std::clamp(point.x, 0.0f,
        static_cast<float>((std::max)(0, ImageState::Width() - 1)));
    point.y = std::clamp(point.y, 0.0f,
        static_cast<float>((std::max)(0, ImageState::Height() - 1)));
    return point;
}

bool MouseInsideImage(const ImVec2& mouse)
{
    if (!ImageState::HasImage())
        return false;
    const ImVec2 first = ToScreen({0.0f, 0.0f});
    const ImVec2 last = ToScreen({static_cast<float>(ImageState::Width()),
                                  static_cast<float>(ImageState::Height())});
    return mouse.x >= (std::min)(first.x, last.x) &&
           mouse.x <= (std::max)(first.x, last.x) &&
           mouse.y >= (std::min)(first.y, last.y) &&
           mouse.y <= (std::max)(first.y, last.y);
}

GeometryPrimitive NewPrimitive(const ToolInstance& tool, GeometryPrimitiveType type)
{
    GeometryPrimitive item;
    item.type = type;
    int number = 1;
    for (const GeometryPrimitive& existing : tool.geometryItems)
        if (existing.type == type) ++number;
    item.name = std::string(GeometryPrimitiveTypeName(type)) + " " + std::to_string(number);
    item.color = {0, 220, 110, 255};
    item.text = "文字";
    return item;
}

bool IsDragCreated(GeometryPrimitiveType type)
{
    return type == GeometryPrimitiveType::Line ||
           type == GeometryPrimitiveType::Rectangle ||
           type == GeometryPrimitiveType::RotatedRectangle ||
           type == GeometryPrimitiveType::Circle ||
           type == GeometryPrimitiveType::Arrow;
}

bool IsFillable(GeometryPrimitiveType type)
{
    return type == GeometryPrimitiveType::Rectangle ||
           type == GeometryPrimitiveType::RotatedRectangle ||
           type == GeometryPrimitiveType::Circle ||
           type == GeometryPrimitiveType::Polygon;
}

void CommitDraft(ToolInstance& tool)
{
    EditorState& state = State();
    bool valid = state.hasDraft;
    if (state.draft.type == GeometryPrimitiveType::Polygon)
        valid = valid && state.draft.points.size() >= 3;
    else if (state.draft.type == GeometryPrimitiveType::Cross ||
             state.draft.type == GeometryPrimitiveType::Text)
        valid = valid && !state.draft.points.empty();
    else
        valid = valid && state.draft.points.size() >= 2 &&
                cv::norm(state.draft.points[1] - state.draft.points[0]) >= 2.0;
    if (valid)
    {
        NormalizeGeometryPrimitive(state.draft);
        tool.geometryItems.push_back(std::move(state.draft));
        state.selectedIndex = static_cast<int>(tool.geometryItems.size()) - 1;
        state.changed = true;
    }
    state.hasDraft = false;
    state.draft = {};
}

ImU32 ItemColor(const GeometryPrimitive& item, int alpha = -1)
{
    return IM_COL32(item.color[0], item.color[1], item.color[2],
                    alpha >= 0 ? alpha : item.color[3]);
}

std::vector<cv::Point2f> RectangleCorners(const GeometryPrimitive& item)
{
    if (item.points.size() < 2)
        return {};
    if (item.type == GeometryPrimitiveType::RotatedRectangle)
        return GeometryPrimitiveRotatedCorners(item);
    const float left = (std::min)(item.points[0].x, item.points[1].x);
    const float right = (std::max)(item.points[0].x, item.points[1].x);
    const float top = (std::min)(item.points[0].y, item.points[1].y);
    const float bottom = (std::max)(item.points[0].y, item.points[1].y);
    return {{left, top}, {right, top}, {right, bottom}, {left, bottom}};
}

cv::Point2f RotationHandle(const GeometryPrimitive& item)
{
    const cv::Point2f center = GeometryPrimitiveCenter(item);
    if (item.points.size() < 2)
        return center;
    const float halfHeight = std::abs(item.points[1].y - item.points[0].y) * 0.5f;
    const float radians = item.angle * static_cast<float>(CV_PI / 180.0);
    const cv::Point2f direction(std::sin(radians), -std::cos(radians));
    return center + direction * (halfHeight + 30.0f / (std::max)(CanvasZoom(), 0.01f));
}

void DrawItem(ImDrawList* drawList, const GeometryPrimitive& item, bool selected)
{
    if (!item.visible || item.points.empty())
        return;
    const ImU32 color = ItemColor(item, selected ? 255 : item.color[3]);
    const ImU32 fill = ItemColor(item, (std::min)(item.color[3], 90));
    const float thickness = (std::max)(1.0f, item.thickness * (std::max)(CanvasZoom(), 0.25f));
    if (item.type == GeometryPrimitiveType::Line && item.points.size() >= 2)
        drawList->AddLine(ToScreen(item.points[0]), ToScreen(item.points[1]), color, thickness);
    else if (item.type == GeometryPrimitiveType::Arrow && item.points.size() >= 2)
    {
        const ImVec2 first = ToScreen(item.points[0]);
        const ImVec2 last = ToScreen(item.points[1]);
        drawList->AddLine(first, last, color, thickness);
        const float angle = std::atan2(last.y - first.y, last.x - first.x);
        const float length = 12.0f;
        drawList->AddTriangleFilled(last,
            ImVec2(last.x - std::cos(angle - 0.5f) * length,
                   last.y - std::sin(angle - 0.5f) * length),
            ImVec2(last.x - std::cos(angle + 0.5f) * length,
                   last.y - std::sin(angle + 0.5f) * length), color);
    }
    else if ((item.type == GeometryPrimitiveType::Rectangle ||
              item.type == GeometryPrimitiveType::RotatedRectangle) && item.points.size() >= 2)
    {
        const auto corners = RectangleCorners(item);
        if (corners.size() == 4)
        {
            ImVec2 screen[4];
            for (int i = 0; i < 4; ++i) screen[i] = ToScreen(corners[i]);
            if (item.filled) drawList->AddConvexPolyFilled(screen, 4, fill);
            drawList->AddPolyline(screen, 4, color, ImDrawFlags_Closed, thickness);
        }
    }
    else if (item.type == GeometryPrimitiveType::Circle && item.points.size() >= 2)
    {
        const float radius = static_cast<float>(cv::norm(item.points[1] - item.points[0])) * CanvasZoom();
        if (item.filled) drawList->AddCircleFilled(ToScreen(item.points[0]), radius, fill, 48);
        drawList->AddCircle(ToScreen(item.points[0]), radius, color, 48, thickness);
    }
    else if (item.type == GeometryPrimitiveType::Cross)
    {
        const ImVec2 center = ToScreen(item.points[0]);
        const float size = item.crossSize * CanvasZoom();
        drawList->AddLine({center.x - size, center.y}, {center.x + size, center.y}, color, thickness);
        drawList->AddLine({center.x, center.y - size}, {center.x, center.y + size}, color, thickness);
    }
    else if (item.type == GeometryPrimitiveType::Text)
    {
        drawList->AddText(ToScreen(item.points[0]), color, item.text.c_str());
    }
    else if (item.type == GeometryPrimitiveType::Polygon && item.points.size() >= 2)
    {
        std::vector<ImVec2> points;
        for (const cv::Point2f& point : item.points) points.push_back(ToScreen(point));
        if (item.filled && points.size() >= 3)
            drawList->AddConcavePolyFilled(points.data(), static_cast<int>(points.size()), fill);
        drawList->AddPolyline(points.data(), static_cast<int>(points.size()), color,
            points.size() >= 3 ? ImDrawFlags_Closed : ImDrawFlags_None, thickness);
    }
}

void DrawHandle(ImDrawList* drawList, const cv::Point2f& point, ImU32 color)
{
    const ImVec2 screen = ToScreen(point);
    drawList->AddCircleFilled(screen, kHandleRadius + 1.0f, IM_COL32(20, 24, 28, 240));
    drawList->AddCircleFilled(screen, kHandleRadius - 1.0f, color);
}

void DrawHandles(ImDrawList* drawList, const GeometryPrimitive& item)
{
    const ImU32 color = IM_COL32(255, 220, 80, 255);
    if (item.type == GeometryPrimitiveType::RotatedRectangle)
    {
        const auto corners = GeometryPrimitiveRotatedCorners(item);
        for (const cv::Point2f& point : corners) DrawHandle(drawList, point, color);
        const cv::Point2f center = GeometryPrimitiveCenter(item);
        const cv::Point2f rotate = RotationHandle(item);
        drawList->AddLine(ToScreen(center), ToScreen(rotate), color, 1.0f);
        DrawHandle(drawList, rotate, IM_COL32(255, 140, 50, 255));
    }
    else
    {
        for (const cv::Point2f& point : item.points) DrawHandle(drawList, point, color);
    }
}

float DistanceSquared(const ImVec2& first, const ImVec2& second)
{
    const float dx = first.x - second.x;
    const float dy = first.y - second.y;
    return dx * dx + dy * dy;
}

float SegmentDistance(const ImVec2& point, const ImVec2& first, const ImVec2& last)
{
    const float dx = last.x - first.x;
    const float dy = last.y - first.y;
    const float lengthSquared = dx * dx + dy * dy;
    if (lengthSquared < 0.001f)
        return std::sqrt(DistanceSquared(point, first));
    const float t = std::clamp(((point.x - first.x) * dx + (point.y - first.y) * dy) /
                               lengthSquared, 0.0f, 1.0f);
    return std::sqrt(DistanceSquared(point, {first.x + dx * t, first.y + dy * t}));
}

bool HitHandle(const GeometryPrimitive& item, const ImVec2& mouse, int& point, DragMode& mode)
{
    const float limit = (kHandleRadius + 5.0f) * (kHandleRadius + 5.0f);
    if (item.type == GeometryPrimitiveType::RotatedRectangle)
    {
        if (DistanceSquared(mouse, ToScreen(RotationHandle(item))) <= limit)
        {
            point = -1;
            mode = DragMode::Rotate;
            return true;
        }
        const auto corners = GeometryPrimitiveRotatedCorners(item);
        for (int index = 0; index < static_cast<int>(corners.size()); ++index)
            if (DistanceSquared(mouse, ToScreen(corners[index])) <= limit)
            {
                point = kRotatedCornerBase + index;
                mode = DragMode::Point;
                return true;
            }
        return false;
    }
    for (int index = 0; index < static_cast<int>(item.points.size()); ++index)
        if (DistanceSquared(mouse, ToScreen(item.points[index])) <= limit)
        {
            point = index;
            mode = DragMode::Point;
            return true;
        }
    return false;
}

bool HitBody(const GeometryPrimitive& item, const ImVec2& mouse)
{
    if (!item.visible || item.points.empty())
        return false;
    const cv::Point2f image = ToImage(mouse);
    if ((item.type == GeometryPrimitiveType::Line || item.type == GeometryPrimitiveType::Arrow) &&
        item.points.size() >= 2)
        return SegmentDistance(mouse, ToScreen(item.points[0]), ToScreen(item.points[1])) <= kHitDistance;
    if (item.type == GeometryPrimitiveType::Circle && item.points.size() >= 2)
        return cv::norm(image - item.points[0]) <= cv::norm(item.points[1] - item.points[0]) +
            kHitDistance / (std::max)(CanvasZoom(), 0.01f);
    if (item.type == GeometryPrimitiveType::Cross)
        return std::abs(image.x - item.points[0].x) <= item.crossSize &&
               std::abs(image.y - item.points[0].y) <= item.crossSize;
    if (item.type == GeometryPrimitiveType::Text)
        return image.x >= item.points[0].x && image.y >= item.points[0].y &&
               image.x <= item.points[0].x + item.text.size() * item.fontSize * 0.6f &&
               image.y <= item.points[0].y + item.fontSize;
    std::vector<cv::Point2f> polygon = item.type == GeometryPrimitiveType::Polygon
        ? item.points : RectangleCorners(item);
    return polygon.size() >= 3 && cv::pointPolygonTest(polygon, image, false) >= 0.0;
}

void ResizeRotated(GeometryPrimitive& item, const cv::Point2f& mouse)
{
    const cv::Point2f center = GeometryPrimitiveCenter(item);
    const float radians = -item.angle * static_cast<float>(CV_PI / 180.0);
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const cv::Point2f delta = mouse - center;
    const float localX = delta.x * cosine - delta.y * sine;
    const float localY = delta.x * sine + delta.y * cosine;
    const cv::Point2f half((std::max)(2.0f, std::abs(localX)),
                           (std::max)(2.0f, std::abs(localY)));
    item.points[0] = center - half;
    item.points[1] = center + half;
}
}

void Cancel()
{
    State() = {};
}

bool DrawToolPanel(ToolInstance& tool, int toolIndex)
{
    EditorState& state = State();
    if (state.toolIndex != toolIndex && state.dragMode == DragMode::None)
    {
        state.toolIndex = toolIndex;
        state.selectedIndex = tool.geometryItems.empty() ? -1 : 0;
        state.creating = false;
        state.hasDraft = false;
    }
    bool changed = false;
    const char* names[] = {"直线", "矩形", "旋转矩形", "圆", "箭头", "十字标记", "文字", "多边形"};
    tool.geometryDrawType = std::clamp(tool.geometryDrawType, 0, 7);
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::Combo("##geometry_type", &tool.geometryDrawType, names, IM_ARRAYSIZE(names));
    if (state.creating)
    {
        if (ImGui::Button("停止绘制", ImVec2(-1.0f, 0.0f)))
        {
            state.creating = false;
            state.hasDraft = false;
            state.dragMode = DragMode::None;
        }
    }
    else if (ImGui::Button("开始绘制", ImVec2(-1.0f, 0.0f)))
    {
        state.createType = static_cast<GeometryPrimitiveType>(tool.geometryDrawType);
        state.creating = true;
        state.hasDraft = false;
        state.dragMode = DragMode::None;
    }

    ImGui::TextDisabled("图形 %zu", tool.geometryItems.size());
    ImGui::BeginChild("##geometry_items", ImVec2(0.0f, 100.0f), true);
    for (int index = 0; index < static_cast<int>(tool.geometryItems.size()); ++index)
    {
        GeometryPrimitive& item = tool.geometryItems[index];
        ImGui::PushID(index);
        changed |= ImGui::Checkbox("##visible", &item.visible);
        ImGui::SameLine();
        if (ImGui::Selectable(item.name.c_str(), state.selectedIndex == index))
            state.selectedIndex = index;
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (state.selectedIndex >= 0 && state.selectedIndex < static_cast<int>(tool.geometryItems.size()))
    {
        if (ImGui::Button("删除选中"))
        {
            tool.geometryItems.erase(tool.geometryItems.begin() + state.selectedIndex);
            state.selectedIndex = tool.geometryItems.empty() ? -1 :
                (std::min)(state.selectedIndex, static_cast<int>(tool.geometryItems.size()) - 1);
            changed = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("清空") && !tool.geometryItems.empty())
    {
        tool.geometryItems.clear();
        state.selectedIndex = -1;
        changed = true;
    }

    if (state.selectedIndex >= 0 && state.selectedIndex < static_cast<int>(tool.geometryItems.size()))
    {
        GeometryPrimitive& item = tool.geometryItems[state.selectedIndex];
        char nameBuffer[128]{};
        std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", item.name.c_str());
        if (ImGui::InputText("名称", nameBuffer, IM_ARRAYSIZE(nameBuffer)))
        {
            item.name = nameBuffer;
            changed = true;
        }
        float color[4] = {item.color[0] / 255.0f, item.color[1] / 255.0f,
                          item.color[2] / 255.0f, item.color[3] / 255.0f};
        if (ImGui::ColorEdit4("颜色", color, ImGuiColorEditFlags_AlphaBar))
        {
            for (int channel = 0; channel < 4; ++channel)
                item.color[channel] = cvRound(color[channel] * 255.0f);
            changed = true;
        }
        changed |= ImGui::SliderInt("线宽", &item.thickness, 1, 32);
        if (IsFillable(item.type)) changed |= ImGui::Checkbox("填充", &item.filled);
        if (item.type == GeometryPrimitiveType::RotatedRectangle)
            changed |= ImGui::DragFloat("角度", &item.angle, 0.1f, -180.0f, 180.0f, "%.2f deg");
        if (item.type == GeometryPrimitiveType::Arrow)
            changed |= ImGui::SliderFloat("箭头比例", &item.arrowTipLength, 0.05f, 0.8f, "%.2f");
        if (item.type == GeometryPrimitiveType::Cross)
            changed |= ImGui::SliderInt("尺寸", &item.crossSize, 1, 500);
        if (item.type == GeometryPrimitiveType::Text)
        {
            char textBuffer[512]{};
            std::snprintf(textBuffer, sizeof(textBuffer), "%s", item.text.c_str());
            if (ImGui::InputTextMultiline("文字", textBuffer, IM_ARRAYSIZE(textBuffer), ImVec2(-1, 50)))
            {
                item.text = textBuffer;
                changed = true;
            }
            changed |= ImGui::SliderInt("字号", &item.fontSize, 6, 128);
        }
        if (ImGui::CollapsingHeader("坐标"))
        {
            for (int index = 0; index < static_cast<int>(item.points.size()); ++index)
            {
                float point[2] = {item.points[index].x, item.points[index].y};
                char label[32];
                std::snprintf(label, sizeof(label), "P%d", index + 1);
                if (ImGui::InputFloat2(label, point, "%.3f"))
                {
                    item.points[index] = {point[0], point[1]};
                    changed = true;
                }
            }
        }
        if (changed) NormalizeGeometryPrimitive(item);
    }
    if (changed) state.changed = true;
    return changed;
}

void DrawCanvasOverlay(ImDrawList* drawList)
{
    ToolInstance* tool = ActiveTool();
    if (!tool || !drawList)
        return;
    EditorState& state = State();
    for (int index = 0; index < static_cast<int>(tool->geometryItems.size()); ++index)
    {
        DrawItem(drawList, tool->geometryItems[index], index == state.selectedIndex);
        if (index == state.selectedIndex && tool->geometryItems[index].visible)
            DrawHandles(drawList, tool->geometryItems[index]);
    }
    if (state.hasDraft)
        DrawItem(drawList, state.draft, true);
}

bool IsCanvasActive()
{
    return ActiveTool() != nullptr;
}

bool HandleCanvasInteraction()
{
    ToolInstance* tool = ActiveTool();
    if (!tool)
        return false;
    EditorState& state = State();
    const ImVec2 mouseScreen = ImGui::GetMousePos();
    const bool inside = MouseInsideImage(mouseScreen);
    const cv::Point2f mouse = ClampToImage(ToImage(mouseScreen));

    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        state.creating = false;
        state.hasDraft = false;
        state.dragMode = DragMode::None;
    }
    if (state.creating)
    {
        if (state.createType == GeometryPrimitiveType::Polygon)
        {
            if (inside && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                if (!state.hasDraft)
                {
                    state.draft = NewPrimitive(*tool, state.createType);
                    state.hasDraft = true;
                }
                state.draft.points.push_back(mouse);
            }
            if (state.hasDraft && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)))
            {
                CommitDraft(*tool);
                state.creating = false;
            }
            return true;
        }
        if (inside && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            state.draft = NewPrimitive(*tool, state.createType);
            state.draft.points.push_back(mouse);
            state.hasDraft = true;
            if (state.createType == GeometryPrimitiveType::Cross ||
                state.createType == GeometryPrimitiveType::Text)
            {
                CommitDraft(*tool);
                state.creating = false;
            }
            else if (IsDragCreated(state.createType))
            {
                state.draft.points.push_back(mouse);
                state.dragMode = DragMode::Create;
            }
        }
        if (state.dragMode == DragMode::Create && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            state.draft.points[1] = mouse;
        if (state.dragMode == DragMode::Create && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            CommitDraft(*tool);
            state.dragMode = DragMode::None;
            state.creating = false;
        }
        return true;
    }

    if (state.selectedIndex >= 0 && state.selectedIndex < static_cast<int>(tool->geometryItems.size()) &&
        ImGui::IsKeyPressed(ImGuiKey_Delete) && !ImGui::GetIO().WantTextInput)
    {
        tool->geometryItems.erase(tool->geometryItems.begin() + state.selectedIndex);
        state.selectedIndex = tool->geometryItems.empty() ? -1 :
            (std::min)(state.selectedIndex, static_cast<int>(tool->geometryItems.size()) - 1);
        state.changed = true;
    }
    if (inside && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        int point = -1;
        DragMode mode = DragMode::None;
        if (state.selectedIndex >= 0 && state.selectedIndex < static_cast<int>(tool->geometryItems.size()) &&
            HitHandle(tool->geometryItems[state.selectedIndex], mouseScreen, point, mode))
        {
            state.dragMode = mode;
            state.dragPoint = point;
            state.lastMouse = mouse;
        }
        else
        {
            state.selectedIndex = -1;
            for (int index = static_cast<int>(tool->geometryItems.size()) - 1; index >= 0; --index)
                if (HitBody(tool->geometryItems[index], mouseScreen))
                {
                    state.selectedIndex = index;
                    state.dragMode = DragMode::Move;
                    state.lastMouse = mouse;
                    break;
                }
        }
    }
    if (state.dragMode != DragMode::None && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        state.selectedIndex >= 0 && state.selectedIndex < static_cast<int>(tool->geometryItems.size()))
    {
        GeometryPrimitive& item = tool->geometryItems[state.selectedIndex];
        if (state.dragMode == DragMode::Move)
        {
            TranslateGeometryPrimitive(item, mouse - state.lastMouse);
            state.lastMouse = mouse;
        }
        else if (state.dragMode == DragMode::Rotate)
        {
            const cv::Point2f center = GeometryPrimitiveCenter(item);
            item.angle = static_cast<float>(std::atan2(mouse.y - center.y, mouse.x - center.x) *
                                             180.0 / CV_PI + 90.0);
            NormalizeGeometryPrimitive(item);
        }
        else if (state.dragMode == DragMode::Point)
        {
            if (state.dragPoint >= kRotatedCornerBase)
                ResizeRotated(item, mouse);
            else if (state.dragPoint >= 0 && state.dragPoint < static_cast<int>(item.points.size()))
                item.points[state.dragPoint] = mouse;
        }
        state.changed = true;
    }
    if (state.dragMode != DragMode::None && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        state.dragMode = DragMode::None;
        state.dragPoint = -1;
    }
    return true;
}

bool ConsumeChanged()
{
    EditorState& state = State();
    if (!state.changed || state.dragMode != DragMode::None)
        return false;
    state.changed = false;
    return true;
}
}
