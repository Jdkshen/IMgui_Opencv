#pragma once

#include <string>

// =====================================================
// ImageLoadController — 图片加载调度器
// 每帧调用 Update()，协调 FrameNavigation/显式请求 → 异步加载 → GPU 上传
// =====================================================
namespace ImageLoadController
{
    void RequestLoad(std::string path);
    // Abandon both queued and in-flight file loads when another source takes
    // ownership of the public preview (camera/video/task camera).
    void CancelPending();
    void Update();  // 主循环每帧调用：检查待加载请求、调度异步加载、处理加载完成回调
    bool ConsumeLastError(std::string& error);
}
