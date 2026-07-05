#pragma once

// =====================================================
// LiveYoloRunner — YOLO 实时检测调度器
// 每帧调用 Update()，在视频/摄像头播放时持续运行 YOLO 推理
// 支持两种后端：YOLODetector（ONNX Runtime GPU）和 OpenCVYoloDetector（OpenCV DNN CPU）
// =====================================================
namespace LiveYoloRunner
{
    void Update();  // 主循环每帧调用：检查实时检测开关 → 获取帧 → 推理 → 发布结果
}
