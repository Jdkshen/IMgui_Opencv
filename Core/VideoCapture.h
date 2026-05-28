#pragma once
#include <string>

namespace VideoCapture
{
    // 打开视频文件
    bool OpenVideo(const std::string& path);

    // 打开摄像头（index=0 默认摄像头）
    bool OpenCamera(int index = 0);

    // 关闭当前视频/摄像头
    void Close();

    // 状态查询
    bool IsOpen();
    bool IsPlaying();
    bool IsCamera();

    // 播放控制
    void Play();
    void Pause();
    void TogglePlay();
    void Stop();

    // 循环播放
    void SetLoop(bool loop);
    bool IsLooping();

    // 每帧调用：如果正在播放且时间已到，抓取下一帧并标记 GPU 上传
    // 返回 true 表示本帧有新画面
    bool Update();

    // 视频信息
    int    GetFrameCount();
    int    GetCurrentFrame();
    double GetFPS();
    double GetPositionSec();

    // 跳转到指定帧
    void SeekFrame(int frame);
}
