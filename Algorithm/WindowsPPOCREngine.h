#pragma once

#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

#include <memory>
#include <stop_token>
#include <string>
#include <vector>

struct PPOCRTextResult
{
    std::string text;
    cv::Rect box;
    float confidence = 0.0f;
};

struct WindowsPPOCRConfig
{
    std::string detParamPath;
    std::string detModelPath;
    std::string recParamPath;
    std::string recModelPath;
    std::string dictionaryPath;
    int inputSize = 512;
    float minConfidence = 0.30f;
    int maxItems = 8;
    int maxCandidates = 220;
    int minBoxArea = 0;
    int minBoxHeight = 0;
    bool fastMode = true;
    bool detectOnly = false;
    bool useGPU = false;
};

struct WindowsPPOCRStats
{
    int inputWidth = 0;
    int inputHeight = 0;
    int resizedWidth = 0;
    int resizedHeight = 0;
    int contours = 0;
    int candidates = 0;
    int recognizedCandidates = 0;
    int readableTexts = 0;
    int workers = 0;
    double preprocessMs = 0.0;
    double detectMs = 0.0;
    double postprocessMs = 0.0;
    double recognizeMs = 0.0;
    double totalMs = 0.0;
};

class WindowsPPOCREngine
{
public:
    WindowsPPOCREngine();
    ~WindowsPPOCREngine();

    WindowsPPOCREngine(const WindowsPPOCREngine&) = delete;
    WindowsPPOCREngine& operator=(const WindowsPPOCREngine&) = delete;

    bool Load(const WindowsPPOCRConfig& cfg, std::string* error);
    bool Recognize(const cv::Mat& bgr, std::vector<PPOCRTextResult>& out, std::string* error,
        std::stop_token stopToken = {});
    bool IsReady() const;
    WindowsPPOCRStats LastStats() const;

    static std::string ResolvePathForTest(const std::string& path);
    static cv::Size RecognitionCropSizeForTest(float rectWidth, float rectHeight, int orientation);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
