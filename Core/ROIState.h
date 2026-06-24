#pragma once

#include <vector>

#include "ROI.h"

namespace ROIState
{
    std::vector<ROI>& Items();
    const std::vector<ROI>& ReadOnlyItems();
    int& SelectedIndexRef();
    int SelectedIndex();
    void SetSelectedIndex(int index);
    int SelectIndexFor(const std::vector<ROI>& rois);
    void ClearInteraction();
}
