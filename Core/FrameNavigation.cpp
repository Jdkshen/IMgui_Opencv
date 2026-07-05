#include "FrameNavigation.h"

#include "LegacyAppState.h"
#include "UIStateBridge.h"

// =====================================================
// FrameNavigation — 图片序列导航实现
// 所有函数直接委托到 LegacyAppState / UIStateBridge 的全局变量
// =====================================================
namespace FrameNavigation
{
const std::vector<std::string>& ImageList()
{
    return gImageList;  // 全局图片路径列表
}

int CurrentImageIndex()
{
    return gCurrentImageIndex;
}

bool IsCurrentImage(const std::string& path)
{
    const int index = CurrentImageIndex();
    const auto& images = ImageList();
    return index >= 0 && index < static_cast<int>(images.size()) && images[index] == path;
}

bool HasNextImage()
{
    const int index = CurrentImageIndex();
    const auto& images = ImageList();
    return !images.empty() && index >= 0 && index < static_cast<int>(images.size()) - 1;
}

void FitImageToWindow()
{
    UI::FitImageToWindow();
}

void NavigateNextImage()
{
    UI::NavigateNextImage();
}
}
