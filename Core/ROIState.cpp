#include "ROIState.h"

#include <algorithm>

// =====================================================
// 内部状态（模块私有）
// =====================================================
namespace ROIState
{
namespace
{
    std::vector<ROI> s_rois;     // ROI 列表
    std::vector<ROI> s_queuedRestore;
    bool s_hasQueuedRestore = false;
    int s_selectedROI = -1;      // 当前选中 ROI 索引（-1 = 无选中）
}

const std::vector<ROI>& ReadOnlyItems()
{
    return s_rois;
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

int Insert(int index, ROI roi, bool select)
{
    index = std::clamp(index, 0, static_cast<int>(s_rois.size()));
    s_rois.insert(s_rois.begin() + index, std::move(roi));
    if (select)
        s_selectedROI = index;
    else if (s_selectedROI >= index)
        ++s_selectedROI;
    return index;
}

bool Update(int index, ROI roi)
{
    if (!IsValidIndex(index))
        return false;
    s_rois[index] = std::move(roi);
    return true;
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

std::size_t RemoveByType(int type)
{
    const std::size_t before = s_rois.size();
    s_rois.erase(std::remove_if(s_rois.begin(), s_rois.end(),
        [type](const ROI& roi) { return roi.type == type; }), s_rois.end());
    s_selectedROI = -1;
    return before - s_rois.size();
}

void ReplaceAll(std::vector<ROI> rois, int selectedIndex)
{
    s_rois = std::move(rois);
    s_selectedROI = selectedIndex >= 0 && selectedIndex < static_cast<int>(s_rois.size())
        ? selectedIndex : -1;
}

void Clear()
{
    s_rois.clear();
    s_selectedROI = -1;
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

void QueueRestoreAfterImageLoad(std::vector<ROI> rois)
{
    s_queuedRestore = std::move(rois);
    s_hasQueuedRestore = true;
}

bool ApplyQueuedRestore()
{
    if (!s_hasQueuedRestore)
        return false;
    s_rois = std::move(s_queuedRestore);
    s_queuedRestore.clear();
    s_hasQueuedRestore = false;
    s_selectedROI = -1;
    return true;
}

void CancelQueuedRestore()
{
    s_queuedRestore.clear();
    s_hasQueuedRestore = false;
}

bool HasQueuedRestore()
{
    return s_hasQueuedRestore;
}

void ClearInteraction()
{
    Clear();
}
}
