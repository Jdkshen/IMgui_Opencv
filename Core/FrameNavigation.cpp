#include "FrameNavigation.h"
#include "VideoCapture.h"

#include <utility>

namespace
{
std::vector<std::string> s_ImageList;
int s_CurrentImageIndex = -1;
bool s_FitRequested = false;
std::string s_PendingImagePath;
}

namespace FrameNavigation
{
const std::vector<std::string>& ImageList() { return s_ImageList; }
std::vector<std::string>& ImageListRef() { return s_ImageList; }
int CurrentImageIndex() { return s_CurrentImageIndex; }
int& CurrentImageIndexRef() { return s_CurrentImageIndex; }

void SetImageList(std::vector<std::string> images)
{
    s_ImageList = std::move(images);
    s_CurrentImageIndex = -1;
    s_PendingImagePath.clear();
}

bool IsCurrentImage(const std::string& path)
{
    return s_CurrentImageIndex >= 0 && s_CurrentImageIndex < static_cast<int>(s_ImageList.size()) &&
           s_ImageList[s_CurrentImageIndex] == path;
}

bool HasNextImage()
{
    return !s_ImageList.empty() && s_CurrentImageIndex >= 0 &&
           s_CurrentImageIndex < static_cast<int>(s_ImageList.size()) - 1;
}

bool NavigateToImage(int index)
{
    if (index < 0 || index >= static_cast<int>(s_ImageList.size()))
        return false;
    s_CurrentImageIndex = index;
    s_PendingImagePath = s_ImageList[index];
    return true;
}

void RequestImagePath(std::string path)
{
    s_PendingImagePath = std::move(path);
}

void NavigateNextImage()
{
    if (HasNextImage())
        NavigateToImage(s_CurrentImageIndex + 1);
}

void FitImageToWindow()
{
    s_FitRequested = true;
}

bool ConsumeFitRequest()
{
    const bool requested = s_FitRequested;
    s_FitRequested = false;
    return requested;
}

bool ConsumePendingImagePath(std::string& path)
{
    if (s_PendingImagePath.empty())
        return false;
    path = std::move(s_PendingImagePath);
    s_PendingImagePath.clear();
    return true;
}

bool OpenVideoSource(const std::string& path)
{
    return VideoCapture::OpenVideo(path);
}

bool OpenCameraSource(int index)
{
    return VideoCapture::OpenCamera(index);
}

PlaybackState CurrentPlayback()
{
    PlaybackState state;
    state.open = VideoCapture::IsOpen();
    state.playing = VideoCapture::IsPlaying();
    state.camera = VideoCapture::IsCamera();
    state.looping = VideoCapture::IsLooping();
    state.frameCount = VideoCapture::GetFrameCount();
    state.currentFrame = VideoCapture::GetCurrentFrame();
    state.fps = VideoCapture::GetFPS();
    return state;
}

void TogglePlayback() { VideoCapture::TogglePlay(); }
void StopPlayback() { VideoCapture::Stop(); }
void ClosePlayback() { VideoCapture::Close(); }
void SeekPlaybackFrame(int frame) { VideoCapture::SeekFrame(frame); }
void SetPlaybackLoop(bool loop) { VideoCapture::SetLoop(loop); }
}
