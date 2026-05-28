#pragma once
#include <string>
#include <atomic>

namespace AudioPlayer
{
    // 打开媒体文件并初始化音频流（内部使用 Media Foundation + XAudio2）
    bool Open(const std::string& path);

    // 关闭并释放音频资源
    void Close();

    // 播放控制
    void Play();
    void Pause();
    void Stop();
    void TogglePlay();

    // 音量 (0.0 ~ 1.0)
    void SetVolume(float vol);

    // 跳转到指定秒数
    void Seek(double seconds);

    // 状态
    bool IsOpen();
    bool IsPlaying();

    // 获取当前播放位置（秒）
    double GetPositionSec();

    // 设置播放速率 (1.0 = 正常)
    void SetRate(float rate);
}
