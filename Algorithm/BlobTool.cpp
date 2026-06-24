#define NOMINMAX
#include "BlobTool.h"
#include "ToolImageUtils.h"
#include "../Core/VisionContext.h"

nlohmann::json BlobTool::Save() const
{
    return {{"type", 2}, {"minArea", minArea}, {"maxArea", maxArea}};
}

void BlobTool::Load(const nlohmann::json &j)
{
    minArea = j.value("minArea", 100);
    maxArea = j.value("maxArea", 10000);
}

ToolResult BlobTool::Execute(VisionContext &ctx)
{
    ToolResult result;
    result.toolName = GetName();
    if (ctx.image.empty())
    {
        result.success = false;
        result.message = "请先加载图片";
        return result;
    }

    const cv::Rect roi = ToolImageUtils::PrimaryContextRect(ctx);
    const cv::Mat input = roi.empty() ? ctx.image : ctx.image(roi);
    cv::Mat gray = ToolImageUtils::ToGray(input);

    cv::Mat bin;
    cv::threshold(gray, bin, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    cv::Mat labels, stats, centroids;
    const int count = cv::connectedComponentsWithStats(bin, labels, stats, centroids, 8, CV_32S);
    result.success = true;
    for (int i = 1; i < count; ++i)
    {
        const int area = stats.at<int>(i, cv::CC_STAT_AREA);
        if (area < minArea || area > maxArea)
            continue;

        ToolResult::Region reg;
        reg.bbox = cv::Rect(
            stats.at<int>(i, cv::CC_STAT_LEFT) + roi.x,
            stats.at<int>(i, cv::CC_STAT_TOP) + roi.y,
            stats.at<int>(i, cv::CC_STAT_WIDTH),
            stats.at<int>(i, cv::CC_STAT_HEIGHT));
        reg.area = static_cast<float>(area);
        reg.score = 1.0f;
        reg.label = "Blob " + std::to_string(area);
        result.regions.push_back(reg);
    }
    return result;
}
