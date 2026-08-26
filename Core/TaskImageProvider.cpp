#include "TaskImageProvider.h"

#include "OpenFileDialog.h"
#include "ToolChainState.h"
#include "../Log/LogSystem.h"

#include <opencv2/imgcodecs.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace TaskImageProvider
{
cv::Mat ReadImageFile(const std::string& imagePath)
{
    if (imagePath.empty())
        return {};
    const auto* begin = reinterpret_cast<const char8_t*>(imagePath.data());
    const std::filesystem::path path(
        std::u8string(begin, begin + imagePath.size()));
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return {};
    const std::vector<uchar> bytes(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    return bytes.empty() ? cv::Mat() : cv::imdecode(bytes, cv::IMREAD_COLOR);
}

bool ResolvePathForRun(const std::string& groupName, std::string& imagePath)
{
    const int index = ToolChainState::TaskGroupIndexByName(groupName);
    if (index < 0)
        return false;

    const TaskGroupDefinition group =
        ToolChainState::ReadOnlyTaskGroups()[index];
    if (group.imageFolderPath.empty())
    {
        imagePath = group.imagePath;
        return !imagePath.empty();
    }

    const std::vector<std::string> images =
        ScanImageFiles(group.imageFolderPath, true);
    if (images.empty())
    {
        LogSystem::Add(LOG_ERROR,
            "任务图片文件夹中没有可用图片 [%s]: %s",
            groupName.c_str(), group.imageFolderPath.c_str());
        return false;
    }

    const int imageCount = static_cast<int>(images.size());
    const int nextIndex = group.imageFolderIndex < 0 ||
        group.imageFolderIndex >= imageCount
        ? 0 : (group.imageFolderIndex + 1) % imageCount;
    imagePath = images[static_cast<std::size_t>(nextIndex)];
    return ToolChainState::SetTaskGroupFolderImagePosition(
        index, imagePath, nextIndex, imageCount);
}
}
