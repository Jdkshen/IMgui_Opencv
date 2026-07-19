#include "ToolChainPreflight.h"

#include "ToolChainValidator.h"

#include <filesystem>

namespace
{
bool Exists(const std::string& path)
{
    if (path.empty())
        return false;

    std::error_code error;
    return std::filesystem::exists(std::filesystem::path(path), error) && !error;
}

void AddMissing(ToolChainPreflightResult& result, int index, const char* resource)
{
    result.issues.push_back({index, std::string("缺少") + resource});
}
}

namespace ToolChainPreflight
{
ToolChainPreflightResult Check(const std::vector<ToolInstance>& tools,
    bool hasImage, std::size_t visibleRoiCount)
{
    ToolChainPreflightResult result;
    const ToolChainValidationResult chain = ToolChainValidator::Validate(tools);
    for (const ToolChainValidationIssue& issue : chain.issues)
        result.issues.push_back({issue.toolIndex, issue.message});

    if (!hasImage && !tools.empty())
        result.issues.push_back({-1, "未加载输入图片"});

    for (int i = 0; i < static_cast<int>(tools.size()); ++i)
    {
        const ToolInstance& tool = tools[i];
        if (!tool.enabled)
            continue;

        if (tool.useSearchROI && tool.searchROIs.empty() && visibleRoiCount == 0)
            AddMissing(result, i, "搜索 ROI");

        switch (tool.type)
        {
        case 1:
            if (tool.templateImg.empty())
                AddMissing(result, i, "模板匹配模板");
            break;
        case 4:
        case 11:
            if (!tool.skipIfModelMissing && !Exists(tool.yoloModelPath))
                AddMissing(result, i, "YOLO 模型");
            break;
        case 6:
            if (tool.shpTplImage.empty())
                AddMissing(result, i, "形状匹配模板");
            break;
        case 10:
            if (tool.mcfRefImage.empty())
                AddMissing(result, i, "找色参考图");
            break;
        case 13:
            if (!tool.skipIfModelMissing)
            {
                if (!Exists(tool.ocrDetModelPath)) AddMissing(result, i, "OCR 检测模型");
                if (!Exists(tool.ocrDetParamPath)) AddMissing(result, i, "OCR 检测参数");
                if (!Exists(tool.ocrRecModelPath)) AddMissing(result, i, "OCR 识别模型");
                if (!Exists(tool.ocrRecParamPath)) AddMissing(result, i, "OCR 识别参数");
                if (!Exists(tool.ocrDictionaryPath)) AddMissing(result, i, "OCR 字典");
            }
            break;
        case 16:
            if (tool.differenceReferenceImage.empty())
                AddMissing(result, i, "差分参考图");
            break;
        default:
            break;
        }
    }
    return result;
}
}
