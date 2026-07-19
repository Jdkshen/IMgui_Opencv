#pragma once

#include "BarcodeTypes.h"

#include <opencv2/core.hpp>

#include <array>
#include <string>
#include <vector>

enum class BarcodeBackendId
{
    OpenCV,
    ZXingCpp,
    ZBar,
    LibDmtx,
    Quirc,
};

enum class BarcodeCoordinatePrecision
{
    Pixel,
    ModelFloat,
};

struct BarcodeBenchmarkItem
{
    std::string text;
    std::string format;
    cv::Rect2f bbox;
    bool hasQuad = false;
    std::array<cv::Point2f, 4> quad{};
};

struct BarcodeBenchmarkResult
{
    BarcodeBackendId backend = BarcodeBackendId::OpenCV;
    std::string backendName;
    bool enabled = false;
    bool success = false;
    BarcodeCoordinatePrecision coordinatePrecision = BarcodeCoordinatePrecision::Pixel;
    std::string message;
    double elapsedMs = 0.0;
    std::vector<BarcodeBenchmarkItem> items;
};

struct BarcodeBackendOptions
{
    bool tryHarder = true;
    int maxSymbols = 32;
    std::uint32_t formatMask = BarcodeFormatAll;
};

namespace BarcodeBenchmark
{
    bool TryBuildOpenCVItem(
        const std::string& text,
        const cv::Mat& points,
        int index,
        cv::Size imageSize,
        BarcodeBenchmarkItem& item);

    BarcodeBenchmarkResult RunBackend(const cv::Mat& image, BarcodeBackendId backend, const BarcodeBackendOptions& options);
    BarcodeBenchmarkResult RunBackend(const cv::Mat& image, BarcodeBackendId backend);
    std::vector<BarcodeBenchmarkResult> RunBarcodeBenchmark(const cv::Mat& image);
}
