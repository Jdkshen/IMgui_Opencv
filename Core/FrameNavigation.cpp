#include "FrameNavigation.h"

#include "LegacyAppState.h"
#include "UIStateBridge.h"

namespace FrameNavigation
{
const std::vector<std::string>& ImageList()
{
    return gImageList;
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
