#include "ImageImportService.h"

#include "FrameNavigation.h"
#include "FrameSourceState.h"
#include "ImageState.h"
#include "OpenFileDialog.h"
#include "ROIState.h"
#include "ToolController.h"
#include "VideoCapture.h"

#include <filesystem>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
std::filesystem::path Utf8Path(const std::string& text)
{
    const auto* begin = reinterpret_cast<const char8_t*>(text.data());
    const std::u8string utf8(begin, begin + text.size());
    return std::filesystem::path(utf8);
}

void ResetInspectionForInputChange()
{
    ToolController::OnInputImageChanged();
    ROIState::ClearInteraction();
}

ImageImportResult NavigationResult(bool success, const std::string& failureMessage)
{
    ImageImportResult result;
    result.success = success;
    result.imageCount = FrameNavigation::ImageList().size();
    result.imageIndex = FrameNavigation::CurrentImageIndex();
    if (success)
    {
        result.imagePath = FrameNavigation::ImageList()[result.imageIndex];
        result.message = "图片加载请求已提交";
    }
    else
    {
        result.message = failureMessage;
    }
    return result;
}
}

namespace ImageImportService
{
ImageImportResult ImportSingleImage(const std::string& imagePath)
{
    ImageImportResult result;
    if (imagePath.empty())
    {
        result.message = "图片路径为空";
        return result;
    }

    std::error_code error;
    const std::filesystem::path path = Utf8Path(imagePath);
    if (!std::filesystem::is_regular_file(path, error))
    {
        result.message = "图片文件不存在或不可访问: " + imagePath;
        return result;
    }

    ResetInspectionForInputChange();
    FrameNavigation::SetImageList({});
    FrameNavigation::RequestImagePath(imagePath);
    result.success = true;
    result.message = "图片加载请求已提交";
    result.imagePath = imagePath;
    return result;
}

ImageImportResult ImportFolder(const std::string& folderPath, bool recursive)
{
    ImageImportResult result;
    if (folderPath.empty())
    {
        result.message = "文件夹路径为空";
        return result;
    }

    std::error_code error;
    const std::filesystem::path path = Utf8Path(folderPath);
    if (!std::filesystem::is_directory(path, error))
    {
        result.message = "文件夹不存在或不可访问: " + folderPath;
        return result;
    }

    std::vector<std::string> images = ScanImageFiles(folderPath, recursive);
    FrameNavigation::SetImageList(std::move(images));
    result.imageCount = FrameNavigation::ImageList().size();
    if (FrameNavigation::ImageList().empty())
    {
        result.message = "所选文件夹及其子目录中没有找到支持的图片文件";
        return result;
    }

    return NavigateToImage(0);
}

ImageImportResult NavigateToImage(int index)
{
    if (!FrameNavigation::NavigateToImage(index))
        return NavigationResult(false, "图片索引无效");

    ResetInspectionForInputChange();
    return NavigationResult(true, {});
}

ImageImportResult NavigatePreviousImage()
{
    return NavigateToImage(FrameNavigation::CurrentImageIndex() - 1);
}

ImageImportResult NavigateNextImage()
{
    return NavigateToImage(FrameNavigation::CurrentImageIndex() + 1);
}

void ClearCurrentInput()
{
    VideoCapture::Close();
    ToolController::OnInputImageChanged();
    ImageState::Clear();
    FrameSourceState::Clear();
    FrameNavigation::SetImageList({});
    ROIState::ClearInteraction();
}
}
