#include "ThresholdTool.h"

#include "../Core/ImageState.h"
#include "../Core/ImageUtils.h"

#include <chrono>

namespace
{
    float s_lastTimeMs = 0.0f;

    cv::Mat Blur(const cv::Mat& image, int size)
    {
        int kernel = size * 2 + 1;
        if (kernel < 3)
            kernel = 3;
        cv::Mat result;
        cv::GaussianBlur(image, result, cv::Size(kernel, kernel), 0);
        return result;
    }

    cv::Mat ToGray(const cv::Mat& image)
    {
        if (image.channels() == 4)
        {
            cv::Mat gray;
            cv::cvtColor(image, gray, cv::COLOR_BGRA2GRAY);
            return gray;
        }
        if (image.channels() == 3)
        {
            cv::Mat gray;
            cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
            return gray;
        }
        return image;
    }

    cv::Mat Threshold(const cv::Mat& image, int value)
    {
        cv::Mat result;
        cv::threshold(ToGray(image), result, value, 255, cv::THRESH_BINARY);
        return result;
    }

    cv::Mat Canny(const cv::Mat& image, int low, int high)
    {
        cv::Mat result;
        cv::Canny(ToGray(image), result, low, high);
        return result;
    }
}

namespace ThresholdTool
{
    void ApplyProcess(bool useGray, const PipelineState& pipeline)
    {
        const cv::Mat& current = ImageState::Current();
        if (current.empty())
            return;

        const auto start = std::chrono::high_resolution_clock::now();
        cv::Mat result = useGray ? ToGray(current) : current;
        if (pipeline.enableBlur)
            result = Blur(result, pipeline.blurSize);

        // Keep the historical precedence: Canny wins when both switches are on.
        if (pipeline.enableCanny)
            result = Canny(result, pipeline.cannyLow, pipeline.cannyHigh);
        else if (pipeline.enableThreshold)
            result = Threshold(result, pipeline.threshold);

        cv::Mat rgba;
        if (result.channels() == 1)
            cv::cvtColor(result, rgba, cv::COLOR_GRAY2RGBA);
        else if (result.channels() == 3)
            cv::cvtColor(result, rgba, cv::COLOR_BGR2RGBA);
        else if (result.channels() == 4)
            cv::cvtColor(result, rgba, cv::COLOR_BGRA2RGBA);
        else
            return;

        ImageState::SetDebugImage(result);
        ImageState::PendingUploadRef() = std::move(rgba);
        ImageState::NeedUploadRef() = true;
        s_lastTimeMs = std::chrono::duration<float, std::milli>(
            std::chrono::high_resolution_clock::now() - start).count();
    }

    float LastTimeMs()
    {
        return s_lastTimeMs;
    }
}
