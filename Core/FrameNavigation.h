#pragma once

#include <string>
#include <vector>

namespace FrameNavigation
{
    const std::vector<std::string>& ImageList();
    int CurrentImageIndex();
    bool IsCurrentImage(const std::string& path);
    bool HasNextImage();
    void FitImageToWindow();
    void NavigateNextImage();
}
