#pragma once
#include "imgui/imgui.h"

// 图像显示/视图变换状态（定义在 ImageViewer.cpp）
extern float gZoom;
extern ImVec2 gPan;
extern ImVec2 gCanvasSize;
extern ImVec2 gImageScreenPos;
extern bool g_ShowPixelGrid; // 像素网格开关
extern bool g_ShowCoordGrid; // 坐标网格开关
extern int g_GridStep;       // 坐标网格步长（图片像素）

// =====================================================
// 图片列表浏览状态（文件夹批量加载）
// =====================================================
extern std::vector<std::string>& gImageList; // Core FrameNavigation 状态别名
extern int& gCurrentImageIndex;

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
