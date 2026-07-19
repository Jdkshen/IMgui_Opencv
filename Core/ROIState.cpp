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
    s_selectedROI = IsValidIndex(index) ? index : -1;
}

bool IsValidIndex(int index)
{
    return index >= 0 && index < static_cast<int>(s_rois.size());
}

ROI* MutableAt(int index)
{
    return IsValidIndex(index) ? &s_rois[index] : nullptr;
}

const ROI* At(int index)
{
    return IsValidIndex(index) ? &s_rois[index] : nullptr;
}

int Add(ROI roi, bool select)
{
    s_rois.push_back(std::move(roi));
    const int index = static_cast<int>(s_rois.size()) - 1;
    if (select)
        s_selectedROI = index;
    return index;
}

bool RemoveAt(int index)
{
    if (!IsValidIndex(index))
        return false;

    s_rois.erase(s_rois.begin() + index);
    if (s_selectedROI == index)
        s_selectedROI = -1;
    else if (s_selectedROI > index)
        --s_selectedROI;
    return true;
}

int FindIndexByRuntimeId(std::uint64_t runtimeId)
{
    if (runtimeId == 0)
        return -1;
    for (int index = 0; index < static_cast<int>(s_rois.size()); ++index)
    {
        if (s_rois[index].runtimeId == runtimeId)
            return index;
    }
    return -1;
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
