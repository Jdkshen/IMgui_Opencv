#include "ROIState.h"

// =====================================================
// 内部状态（模块私有）
// =====================================================
namespace ROIState
{
namespace
{
    std::vector<ROI> s_rois;     // ROI 列表
    int s_selectedROI = -1;      // 当前选中 ROI 索引（-1 = 无选中）
}

std::vector<ROI>& Items()
{
    return s_rois;
}

const std::vector<ROI>& ReadOnlyItems()
{
    return s_rois;
}

int& SelectedIndexRef()
{
    return s_selectedROI;
}

int SelectedIndex()
{
    return s_selectedROI;
}

void SetSelectedIndex(int index)
{
    s_selectedROI = index;
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
    s_rois.clear();
    s_selectedROI = -1;
}
}
