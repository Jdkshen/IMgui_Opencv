#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <vector>
#include <algorithm>
#include <memory>
#include <stop_token>
#include <opencv2/core/mat.hpp>
#include "ROI.h"
#include "../Algorithm/ToolResult.h"
#include "../Algorithm/FrameSource.h"

// =====================================================
// VisionContext — 统一的视觉处理上下文
// 替代散落的 extern 全局变量，作为 ITool::Execute 的唯一入参
// =====================================================
struct VisionContext
{
    // ---- 输入图像 ----
    cv::Mat image;              // 当前处理图像 (BGR, 曾为 gImage)
    cv::Mat originalImage;      // 原始图像备份 (曾为 gOriginalImage)
    std::shared_ptr<const cv::Mat> immutableImageOwner;
    std::shared_ptr<const cv::Mat> immutableOriginalOwner;

    // ---- 帧源（统一输入抽象） ----
    FramePacket frame;          // 当前帧（只读，不关心来源）

    // ---- 图像元数据 ----
    int imageVersion = 0;       // 图像版本号（曾为 g_ImageVersion）
    int width = 0, height = 0;  // 图像尺寸
    std::stop_token stopToken;  // 后台执行取消令牌

    // ---- ROI ----
    std::vector<ROI> rois;      // 本次执行的 ROI 快照
    int selectedROI = -1;       // 本次执行的选中 ROI 快照
    // HALCON 风格定义域。非空时为 CV_8UC1，尺寸与 image 一致；0 表示域外。
    // 图像矩阵尺寸保持不变，算法只在该定义域内产生有效结果。
    cv::Mat domainMask;
    int roiResultPolicy = 0;
    float roiMinimumCoverage = 0.5f;

    // ---- 模板 ----
    cv::Mat frozenTemplate;     // 冻结模板（曾为 g_FrozenTemplate）

    // ---- 统一输出 ----
    std::vector<ToolResult> unifiedResults; // 工具执行结果叠加

    // ---- 便捷函数 ----
    bool HasROI() const { return !rois.empty() && selectedROI >= 0 && selectedROI < (int)rois.size(); }
    bool IsCancellationRequested() const { return stopToken.stop_requested(); }

    // 获取激活ROI的包围矩形（所有类型通用）
    cv::Rect GetActiveROIRect() const
    {
        if (!HasROI()) return cv::Rect();
        return rois[selectedROI].ToCvRect();
    }

    // 获取激活ROI的圆心+半径（CIRCLE 类型专用）
    float GetActiveROIRadius() const
    {
        if (!HasROI() || rois[selectedROI].type != ROI_TYPE_CIRCLE) return 0;
        return rois[selectedROI].CircleRadius();
    }

    // 获取多边形顶点（POLYGON 类型专用）
    const std::vector<ImVec2>& GetActiveROIPolygon() const
    {
        static const std::vector<ImVec2> empty;
        if (!HasROI() || rois[selectedROI].type != ROI_TYPE_POLYGON) return empty;
        return rois[selectedROI].points;
    }

    void ClearUnifiedResults()
    {
        for (auto& result : unifiedResults)
        {
            result.toolName.clear();
            result.message.clear();
            result.measurements.clear();
            for (auto& region : result.regions)
            {
                region.contour.clear();
                region.label.clear();
            }
            result.regions.clear();
            for (auto& detection : result.detections)
                detection.label.clear();
            result.detections.clear();
            result.lines.clear();
            for (auto& text : result.texts)
                text.text.clear();
            result.texts.clear();
            result.debugImage.release();
        }
        unifiedResults.clear();
        unifiedResults.shrink_to_fit();
    }

    void Clear()
    {
        image.release();
        originalImage.release();
        immutableImageOwner.reset();
        immutableOriginalOwner.reset();
        frozenTemplate.release();
        domainMask.release();
        roiResultPolicy = 0;
        roiMinimumCoverage = 0.5f;
        rois.clear();
        selectedROI = -1;
        ClearUnifiedResults();
        frame.clear();
        imageVersion = 0;
        width = height = 0;
        stopToken = {};
    }
};

// =====================================================
// 全局单例上下文（渐进式迁移：逐步替代散落的 extern 变量）
// =====================================================
extern VisionContext gContext;
