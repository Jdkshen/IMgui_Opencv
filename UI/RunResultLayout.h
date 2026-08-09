#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace UI::RunResultLayout
{
    struct Point
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct Size
    {
        float width = 0.0f;
        float height = 0.0f;
    };

    struct Rect
    {
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
    };

    struct DashboardGrid
    {
        int columns = 0;
        int rowCount = 0;
        int visibleRowCount = 0;
        float cardHeight = 0.0f;
    };

    struct LabelPlacement
    {
        bool placed = false;
        Point textPosition;
        Rect bounds;
    };

    Size CalculateDashboardWindowSize(float workWidth, float workHeight);
    DashboardGrid CalculateDashboardGrid(std::size_t itemCount,
        float availableWidth, float gridHeight, float rowSpacing);

    std::string FormatDuration(float durationMs, int fractionalDigits = 1);

    bool RectsOverlap(const Rect& left, const Rect& right);
    LabelPlacement PlaceOverlayLabel(const Point& anchor, const Size& textSize,
        const Rect& imageBounds, const std::vector<Rect>& occupiedLabels);
}
