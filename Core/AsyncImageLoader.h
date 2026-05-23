#pragma once
#include <string>
#include <future>
#include <mutex>
#include <opencv2/opencv.hpp>

// =====================================================
// AsyncImageLoader — 异步图片加载器
// 后台线程读取+解码，主线程检查并上传GPU
// =====================================================
namespace AsyncImageLoader
{
    // 请求加载图片（线程安全，可在UI线程调用）
    void RequestLoad(const std::string& path);

    // 检查异步任务是否完成；若完成则通过回调上传GPU
    // 返回 true 表示本帧处理了结果
    // callback: void(cv::Mat img) — 主线程回调，用于上传纹理
    bool CheckAndProcess(void (*callback)(cv::Mat img));

    // 是否有任务进行中
    bool IsPending();

    // 取消当前任务（不等待）
    void Cancel();
}
