#pragma once

// =====================================================
// ImageLoadController — 图片加载调度器
// 每帧调用 Update()，协调 pendingPath → 异步加载 → GPU 上传的全流程
// =====================================================
namespace ImageLoadController
{
    void Update();  // 主循环每帧调用：检查待加载请求、调度异步加载、处理加载完成回调
}
