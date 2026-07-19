#pragma once

#include <string>
#include <vector>

// =====================================================
// FrameNavigation — 图片序列导航
// 管理文件夹图片列表的浏览：上一张/下一张、适配窗口、索引查询
// =====================================================
namespace FrameNavigation
{
    struct PlaybackState
    {
        bool open = false;
        bool playing = false;
        bool camera = false;
        bool looping = false;
        int frameCount = 0;
        int currentFrame = 0;
        double fps = 0.0;
    };

    const std::vector<std::string>& ImageList();          // 图片路径列表
    std::vector<std::string>& ImageListRef();
    int CurrentImageIndex();                               // 当前图片索引
    int& CurrentImageIndexRef();
    void SetImageList(std::vector<std::string> images);
    bool IsCurrentImage(const std::string& path);          // 判断路径是否为当前图片
    bool HasNextImage();                                   // 是否有下一张图片
    void FitImageToWindow();                               // 缩放图片适配窗口
    void NavigateNextImage();                              // 导航到下一张图片
    bool NavigateToImage(int index);
    void RequestImagePath(std::string path);
    bool ConsumeFitRequest();
    bool ConsumePendingImagePath(std::string& path);

    bool OpenVideoSource(const std::string& path);
    bool OpenCameraSource(int index = 0);
    PlaybackState CurrentPlayback();
    void TogglePlayback();
    void StopPlayback();
    void ClosePlayback();
    void SeekPlaybackFrame(int frame);
    void SetPlaybackLoop(bool loop);
}
