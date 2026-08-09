#include "YOLOTool.h"

#include "../Core/VisionContext.h"
#include "YOLODetector.h"
#include "ToolImageUtils.h"

#include <algorithm>
#include <nlohmann/json.hpp>

ToolResult YOLOTool::Execute(VisionContext& ctx)
{
    ToolResult result;
    result.toolName = "YOLO";

    if (ctx.IsCancellationRequested()) {
        result.success = false;
        result.message = "执行已取消";
        return result;
    }

    if (ctx.image.empty()) {
        result.success = false;
        result.message = "请先加载图片";
        return result;
    }
    if (!ToolImageUtils::ValidateAreaContext(ctx, useROI, result.message)) {
        result.success = false;
        return result;
    }

    if (!modelPath.empty())
        YOLODetector::LoadModel(modelPath, classesPath, useGPU);

    if (ctx.IsCancellationRequested()) {
        result.success = false;
        result.message = "执行已取消";
        return result;
    }

    if (!YOLODetector::IsLoaded()) {
        result.success = false;
        result.message = "model is not loaded";
        return result;
    }

    const cv::Rect roi = ToolImageUtils::PrimaryContextRect(ctx, useROI);

    cv::Mat detectorImage = ctx.image;
    const cv::Mat domainMask = ToolImageUtils::FullContextMask(ctx, useROI);
    if (!domainMask.empty())
    {
        detectorImage = ctx.image.clone();
        ToolImageUtils::ApplyDomainMask(detectorImage, domainMask);
    }
    auto objs = YOLODetector::Detect(detectorImage, confThreshold, nmsThreshold, roi);
    if (ctx.IsCancellationRequested()) {
        result.success = false;
        result.message = "执行已取消";
        return result;
    }
    result.success = true;

    for (const auto& o : objs) {
        ToolResult::Detection d;
        d.box = o.box;
        d.label = o.className;
        d.score = o.confidence;
        d.classId = o.classId;
        result.detections.push_back(d);
    }

    return result;
}

void YOLOTool::DrawUI()
{
    // UI rendering is still delegated to ToolsWindow during the migration.
}

nlohmann::json YOLOTool::Save() const
{
    nlohmann::json j;
    j["type"] = 4;
    j["modelPath"] = modelPath;
    j["classesPath"] = classesPath;
    j["confThreshold"] = confThreshold;
    j["nmsThreshold"] = nmsThreshold;
    j["useROI"] = useROI;
    j["useGPU"] = useGPU;
    return j;
}

void YOLOTool::Load(const nlohmann::json& j)
{
    modelPath = j.value("modelPath", "");
    classesPath = j.value("classesPath", "");
    confThreshold = j.value("confThreshold", 0.5f);
    nmsThreshold = j.value("nmsThreshold", 0.4f);
    useROI = j.value("useROI", false);
    useGPU = j.value("useGPU", false);
}
