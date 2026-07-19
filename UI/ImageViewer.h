#pragma once

#include <string>

namespace UI
{
    void ShowOpenCV();
    void FitImageToWindow();
    void ClearImage();

    // =====================================================
    // 图片列表导航
    // =====================================================
    void LoadFolderImages(const std::string &folderPath); // 从文件夹加载所有图片
    void NavigateToImage(int index);                      // 切换到指定索引的图片
    void NavigatePrevImage();                             // 上一张
    void NavigateNextImage();                             // 下一张
}
