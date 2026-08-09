#pragma once

#include <string>

struct TemplateMatchSettings
{
    bool enableRotation = false;
    int rotationStart = -45;
    int rotationEnd = 45;
    int rotationStep = 1;
    int maxResults = 5;
    float matchThreshold = 0.7f;
    int maxImageDim = 1000;
    float nmsThreshold = 0.3f;
    bool subpixelRefinement = true;
    int searchMode = 0;
    bool templateGray = false;
    bool templateBinary = false;
    int templateBinaryThreshold = 128;
    bool templateEdge = false;
    int templateEdgeLow = 50;
    int templateEdgeHigh = 150;
    bool imageGray = false;
    bool imageThresholdEnabled = false;
    int imageThreshold = 128;
};

struct YoloSettings
{
    std::string modelPath;
    std::string classesPath;
    float confidenceThreshold = 0.5f;
    float nmsThreshold = 0.4f;
    bool useROI = false;
    bool useGPU = false;
};

struct OCRSettings
{
    std::string detectionModelPath = "models\\ppocrv6\\PP_OCRv6_tiny_det.ncnn.bin";
    std::string detectionParamPath = "models\\ppocrv6\\PP_OCRv6_tiny_det.ncnn.param";
    std::string recognitionModelPath = "models\\ppocrv6\\PP_OCRv6_tiny_rec.ncnn.bin";
    std::string recognitionParamPath = "models\\ppocrv6\\PP_OCRv6_tiny_rec.ncnn.param";
    std::string dictionaryPath = "models\\ppocrv6\\ppocr_keys_v6_tiny.txt";
    float minimumConfidence = 0.30f;
    int maximumItems = 8;
    int inputSize = 512;
    int maximumCandidates = 220;
    int minimumBoxArea = 0;
    int minimumBoxHeight = 0;
    int roiPadding = 24;
    bool fastMode = true;
    bool detectOnly = false;
    bool useROI = true;
};

struct ThresholdSettings
{
    bool useGray = false;
    bool enableBlur = false;
    int blurSize = 5;
    bool enableThreshold = false;
    int threshold = 128;
    bool enableCanny = false;
    int cannyLow = 50;
    int cannyHigh = 150;
};

struct BlobSettings
{
    int minArea = 100;
    int maxArea = 10000;
    int thresholdMode = 0;
    int threshold = 128;
    bool invert = false;
    int connectivity = 8;
    float minCircularity = 0.0f;
    float maxCircularity = 1.0f;
    float minAspectRatio = 0.0f;
    float maxAspectRatio = 100.0f;
};

struct MorphologySettings
{
    int operation = 0;
    int kernelSize = 3;
    int kernelShape = 0;
    int iterations = 1;
    bool useGray = false;
};

struct ColorAnalysisSettings
{
    int colorSpace = 0;
    int histogramBins = 32;
    bool showHistogram = true;
    bool useROI = false;
    int histogramHeight = 100;
};
