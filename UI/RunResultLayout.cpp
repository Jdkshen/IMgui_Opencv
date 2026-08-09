#include "RunResultLayout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace UI::RunResultLayout
{
namespace
{
constexpr float kMinimumDashboardWidth = 900.0f;
constexpr float kMaximumDashboardWidth = 1760.0f;
constexpr float kMinimumDashboardHeight = 620.0f;
constexpr float kMaximumDashboardHeight = 1040.0f;
constexpr float kMinimumCardWidth = 270.0f;
constexpr float kMinimumCardHeight = 300.0f;
constexpr float kMaximumCardHeight = 520.0f;
constexpr int kMaximumColumns = 4;
constexpr int kMaximumVisibleRows = 2;
constexpr int kMaximumLabelPlacementAttempts = 10;
constexpr float kLabelPaddingX = 4.0f;
constexpr float kLabelPaddingY = 2.0f;
}

Size CalculateDashboardWindowSize(float workWidth, float workHeight)
{
    return {
        std::clamp(workWidth * 0.96f,
            kMinimumDashboardWidth, kMaximumDashboardWidth),
        std::clamp(workHeight * 0.92f,
            kMinimumDashboardHeight, kMaximumDashboardHeight)
    };
}

DashboardGrid CalculateDashboardGrid(std::size_t itemCount,
    float availableWidth, float gridHeight, float rowSpacing)
{
    if (itemCount == 0)
        return {};

    const int widthLimitedColumns = std::clamp(
        static_cast<int>(std::floor(availableWidth / kMinimumCardWidth)),
        1, kMaximumColumns);
    const int columns = (std::min)(
        static_cast<int>(itemCount), widthLimitedColumns);
    const int rowCount = (static_cast<int>(itemCount) + columns - 1) / columns;
    const int visibleRowCount = (std::min)(rowCount, kMaximumVisibleRows);
    const float heightPerVisibleRow =
        (gridHeight - rowSpacing * (visibleRowCount - 1)) / visibleRowCount;

    return {
        columns,
        rowCount,
        visibleRowCount,
        std::clamp(heightPerVisibleRow - 4.0f,
            kMinimumCardHeight, kMaximumCardHeight)
    };
}

std::string FormatDuration(float durationMs, int fractionalDigits)
{
    if (!std::isfinite(durationMs) || durationMs < 0.0f)
        return "--";

    fractionalDigits = std::clamp(fractionalDigits, 0, 3);
    float unit = 1.0f;
    for (int digit = 0; digit < fractionalDigits; ++digit)
        unit *= 0.1f;
    const float roundingThreshold = unit * 0.5f;
    char text[32]{};
    if (durationMs < roundingThreshold)
    {
        std::snprintf(text, sizeof(text), "<%.*f ms", fractionalDigits, unit);
    }
    else if (durationMs >= 1000.0f)
    {
        std::snprintf(text, sizeof(text), "%.3f s", durationMs / 1000.0f);
    }
    else
    {
        std::snprintf(text, sizeof(text), "%.*f ms", fractionalDigits, durationMs);
    }
    return text;
}

bool RectsOverlap(const Rect& left, const Rect& right)
{
    return left.left < right.right && left.right > right.left &&
        left.top < right.bottom && left.bottom > right.top;
}

LabelPlacement PlaceOverlayLabel(const Point& anchor, const Size& textSize,
    const Rect& imageBounds, const std::vector<Rect>& occupiedLabels)
{
    const float rowHeight = textSize.height + kLabelPaddingY * 2.0f + 4.0f;
    for (int attempt = 0; attempt < kMaximumLabelPlacementAttempts; ++attempt)
    {
        Point position;
        if (attempt == 0)
        {
            position = {
                anchor.x + 2.0f,
                anchor.y - textSize.height - kLabelPaddingY * 2.0f - 2.0f
            };
        }
        else if (attempt == 1)
        {
            position = {anchor.x + 2.0f, anchor.y + 3.0f};
        }
        else
        {
            position = {
                anchor.x + 2.0f,
                imageBounds.top + 4.0f + (attempt - 2) * rowHeight
            };
        }

        const float minimumX = imageBounds.left + kLabelPaddingX + 2.0f;
        const float maximumX = (std::max)(minimumX,
            imageBounds.right - textSize.width - kLabelPaddingX - 2.0f);
        const float minimumY = imageBounds.top + kLabelPaddingY + 2.0f;
        const float maximumY = (std::max)(minimumY,
            imageBounds.bottom - textSize.height - kLabelPaddingY - 2.0f);
        position.x = std::clamp(position.x, minimumX, maximumX);
        position.y = std::clamp(position.y, minimumY, maximumY);

        const Rect bounds = {
            position.x - kLabelPaddingX,
            position.y - kLabelPaddingY,
            position.x + textSize.width + kLabelPaddingX,
            position.y + textSize.height + kLabelPaddingY
        };
        const bool overlaps = std::any_of(
            occupiedLabels.begin(), occupiedLabels.end(),
            [&bounds](const Rect& occupied)
            {
                return RectsOverlap(bounds, occupied);
            });
        if (!overlaps)
            return {true, position, bounds};
    }
    return {};
}
}
