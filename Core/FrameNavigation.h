#pragma once

#include <string>
#include <vector>

// =====================================================
// FrameNavigation — 图片序列导航
// 管理文件夹图片列表的浏览：上一张/下一张、适配窗口、索引查询
// =====================================================
namespace FrameNavigation
{
    const std::vector<std::string>& ImageList();          // 图片路径列表
    int CurrentImageIndex();                               // 当前图片索引
    bool IsCurrentImage(const std::string& path);          // 判断路径是否为当前图片
    bool HasNextImage();                                   // 是否有下一张图片
    void FitImageToWindow();                               // 缩放图片适配窗口
    void NavigateNextImage();                              // 导航到下一张图片
}
