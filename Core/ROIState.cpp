#include "ROIState.h"

#include "UIStateBridge.h"

namespace ROIState
{
std::vector<ROI>& Items()
{
    return UI::gROIs;
}

const std::vector<ROI>& ReadOnlyItems()
{
    return UI::gROIs;
}

int SelectedIndex()
{
    return UI::gSelectedROI;
}

void SetSelectedIndex(int index)
{
    UI::gSelectedROI = index;
}

int SelectIndexFor(const std::vector<ROI>& rois)
{
    if (rois.empty())
        return -1;
    const int selected = SelectedIndex();
    if (selected >= 0 && selected < static_cast<int>(rois.size()))
        return selected;
    return 0;
}

void ClearInteraction()
{
    UI::ClearROIState();
}
}
