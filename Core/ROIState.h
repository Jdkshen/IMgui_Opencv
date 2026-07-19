#pragma once

#include <vector>

#include "ROI.h"

// =====================================================
// ROIState — ROI（感兴趣区域）全局状态管理
// 管理 ROI 列表、当前选中 ROI 索引、交互状态清理
// =====================================================
namespace ROIState
{
    std::vector<ROI>& Items();               // ROI 列表（可写引用）
    const std::vector<ROI>& ReadOnlyItems(); // ROI 列表（只读）
    int& SelectedIndexRef();                 // 当前选中 ROI 索引（可写引用）
    int SelectedIndex();                     // 当前选中 ROI 索引（只读）
    void SetSelectedIndex(int index);        // 设置选中索引

    bool IsValidIndex(int index);
    ROI* MutableAt(int index);
    const ROI* At(int index);
    int Add(ROI roi, bool select = true);
    bool RemoveAt(int index);
    int FindIndexByRuntimeId(std::uint64_t runtimeId);

    // 在给定 ROI 列表中智能选择索引：优先保持当前选中，否则选第一个
    int SelectIndexFor(const std::vector<ROI>& rois);

    // 清空所有 ROI 和交互状态（切换图片时调用）
    void ClearInteraction();
}
