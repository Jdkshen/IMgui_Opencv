#include "BarcodeBenchmark.h"

#include <opencv2/geometry.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>

#ifdef IMGUI_OPENCV_ENABLE_ZXING_CPP
#include <ZXing/ZXingCpp.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>

namespace
{
    double ElapsedMs(std::chrono::steady_clock::time_point start)
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }

    cv::Rect2f ClampRect(const cv::Rect2f& rect, const cv::Size& size)
    {
        const float width = static_cast<float>(size.width);
        const float height = static_cast<float>(size.height);
        const float left = std::clamp(rect.x, 0.0f, width);
        const float top = std::clamp(rect.y, 0.0f, height);
        const float right = std::clamp(rect.x + rect.width, left, width);
        const float bottom = std::clamp(rect.y + rect.height, top, height);
        return cv::Rect2f(left, top, right - left, bottom - top);
    }

    std::vector<cv::Point2f> ReadQuad(const cv::Mat& points, int index)
    {
        std::vector<cv::Point2f> quad;
        if (points.empty() || points.depth() != CV_32F || points.channels() != 2)
            return quad;

        if (points.rows > index && points.cols >= 4)
        {
            for (int i = 0; i < 4; ++i)
            {
                const cv::Vec2f p = points.at<cv::Vec2f>(index, i);
                quad.emplace_back(p[0], p[1]);
            }
            return quad;
        }

        const size_t base = static_cast<size_t>(index) * 4;
        if (points.total() >= base + 4)
        {
            const cv::Vec2f* data = points.ptr<cv::Vec2f>();
            for (int i = 0; i < 4; ++i)
                quad.emplace_back(data[base + i][0], data[base + i][1]);
        }
        return quad;
    }

    cv::Rect2f BoundingBoxFromQuad(const std::vector<cv::Point2f>& quad, const cv::Size& size)
    {
        if (quad.empty())
            return {};

        float minX = quad[0].x;
        float minY = quad[0].y;
        float maxX = quad[0].x;
        float maxY = quad[0].y;
        for (const auto& p : quad)
        {
            minX = (std::min)(minX, p.x);
            minY = (std::min)(minY, p.y);
            maxX = (std::max)(maxX, p.x);
            maxY = (std::max)(maxY, p.y);
        }

        return ClampRect(cv::Rect2f(minX, minY,
            (std::max)(0.0f, maxX - minX), (std::max)(0.0f, maxY - minY)), size);
    }

    bool IsValidOpenCVQuad(const std::vector<cv::Point2f>& quad, const cv::Size& size)
    {
        if (quad.size() != 4 || size.width <= 0 || size.height <= 0)
            return false;

        double signedAreaTwice = 0.0;
        for (size_t i = 0; i < quad.size(); ++i)
        {
            const cv::Point2f& current = quad[i];
            const cv::Point2f& next = quad[(i + 1) % quad.size()];
            if (!std::isfinite(current.x) || !std::isfinite(current.y))
                return false;
            signedAreaTwice += static_cast<double>(current.x) * next.y -
                static_cast<double>(current.y) * next.x;
        }

        const double scale = static_cast<double>((std::max)(size.width, size.height));
        const double areaTolerance = (std::max)(1.0, scale * scale) * 1.0e-7;
        if (std::abs(signedAreaTwice) <= areaTolerance || !cv::isContourConvex(quad))
            return false;

        return !BoundingBoxFromQuad(quad, size).empty();
    }

    BarcodeBenchmarkResult MakeDisabled(BarcodeBackendId backend, const char* name, const char* message)
    {
        BarcodeBenchmarkResult result;
        result.backend = backend;
        result.backendName = name;
        result.enabled = false;
        result.success = false;
        result.message = message;
        return result;
    }

#ifdef IMGUI_OPENCV_ENABLE_ZXING_CPP
    ZXing::BarcodeFormats ZXingFormats(std::uint32_t mask)
    {
        std::vector<ZXing::BarcodeFormat> formats;
        if ((mask & BarcodeFormatQR) != 0)
            formats.push_back(ZXing::BarcodeFormat::QRCode);
        if ((mask & BarcodeFormatCode128) != 0)
            formats.push_back(ZXing::BarcodeFormat::Code128);
        if ((mask & BarcodeFormatEAN) != 0)
        {
            formats.push_back(ZXing::BarcodeFormat::EAN13);
            formats.push_back(ZXing::BarcodeFormat::EAN8);
        }
        if ((mask & BarcodeFormatDataMatrix) != 0)
            formats.push_back(ZXing::BarcodeFormat::DataMatrix);
        if ((mask & BarcodeFormatPDF417) != 0)
            formats.push_back(ZXing::BarcodeFormat::PDF417);
        return ZXing::BarcodeFormats(std::move(formats));
    }

    cv::Rect2f BoundingBoxFromZXingPosition(const ZXing::Position& position, const cv::Size& size)
    {
        float minX = static_cast<float>(position[0].x);
        float minY = static_cast<float>(position[0].y);
        float maxX = minX;
        float maxY = minY;
        for (const auto& p : position)
        {
            minX = (std::min)(minX, static_cast<float>(p.x));
            minY = (std::min)(minY, static_cast<float>(p.y));
            maxX = (std::max)(maxX, static_cast<float>(p.x));
            maxY = (std::max)(maxY, static_cast<float>(p.y));
        }

        return ClampRect(cv::Rect2f(minX, minY,
            (std::max)(0.0f, maxX - minX), (std::max)(0.0f, maxY - minY)), size);
    }

    BarcodeBenchmarkResult RunZXingCpp(const cv::Mat& image, const BarcodeBackendOptions& backendOptions)
    {
        BarcodeBenchmarkResult result;
        result.backend = BarcodeBackendId::ZXingCpp;
        result.backendName = "ZXing-cpp";
        result.enabled = true;
        result.coordinatePrecision = BarcodeCoordinatePrecision::Pixel;

        const auto start = std::chrono::steady_clock::now();
        auto failLayout = [&](const char* message)
        {
            result.elapsedMs = ElapsedMs(start);
            result.message = message;
            return result;
        };
        if (image.rows <= 0 || image.cols <= 0)
            return failLayout("image dimensions are invalid");
        if (image.empty())
            return failLayout("image is empty");
        if (image.depth() != CV_8U)
            return failLayout("unsupported image depth");

        const size_t columns = static_cast<size_t>(image.cols);
        const size_t pixelBytes = static_cast<size_t>(image.channels());
        const size_t sizeMax = (std::numeric_limits<size_t>::max)();
        if (pixelBytes != 0 && columns > sizeMax / pixelBytes)
            return failLayout("image row byte size overflow");
        const size_t rowBytes = columns * pixelBytes;
        const size_t rowStride = image.step[0];
        if (rowStride < rowBytes)
            return failLayout("image row stride is smaller than row byte size");

        const size_t intMax = static_cast<size_t>((std::numeric_limits<int>::max)());
        if (rowStride > intMax)
            return failLayout("image row stride exceeds ZXing integer range");

        size_t minimalSpan = rowBytes;
        if (image.rows > 1)
        {
            const size_t precedingRows = static_cast<size_t>(image.rows - 1);
            if (precedingRows > (sizeMax - rowBytes) / rowStride)
                return failLayout("image data size overflow");
            minimalSpan = precedingRows * rowStride + rowBytes;
        }
        if (minimalSpan > intMax)
            return failLayout("image data size exceeds ZXing integer range");
        if (image.data != nullptr && image.dataend != nullptr)
        {
            if (image.dataend < image.data ||
                minimalSpan > static_cast<size_t>(image.dataend - image.data))
            {
                return failLayout("image data span is smaller than required layout");
            }
        }

        ZXing::ImageFormat format = ZXing::ImageFormat::None;
        if (image.channels() == 1)
            format = ZXing::ImageFormat::Lum;
        else if (image.channels() == 3)
            format = ZXing::ImageFormat::BGR;
        else if (image.channels() == 4)
            format = ZXing::ImageFormat::BGRA;
        else
        {
            result.elapsedMs = ElapsedMs(start);
            result.message = "unsupported image channels";
            return result;
        }

        try
        {
            const int rowStrideBytes = static_cast<int>(rowStride);
            ZXing::ImageView view(image.ptr<uint8_t>(), image.cols, image.rows,
                                  format, rowStrideBytes);
            ZXing::ReaderOptions options;
            const ZXing::BarcodeFormats formats = ZXingFormats(backendOptions.formatMask);
            if (formats.empty())
                return failLayout("no barcode format selected");
            options.setFormats(formats);
            options.setTextMode(ZXing::TextMode::HRI);
            options.setMaxNumberOfSymbols((std::max)(1, backendOptions.maxSymbols));
            options.setTryHarder(backendOptions.tryHarder);

            const ZXing::Barcodes barcodes = ZXing::ReadBarcodes(view, options);
            for (const auto& barcode : barcodes)
            {
                if (!barcode.isValid())
                    continue;

                BarcodeBenchmarkItem item;
                item.text = barcode.text();
                item.format = ZXing::ToString(barcode.format());
                const auto position = barcode.position();
                item.bbox = BoundingBoxFromZXingPosition(position, image.size());
                for (size_t i = 0; i < item.quad.size(); ++i)
                {
                    item.quad[i] = cv::Point2f(static_cast<float>(position[i].x),
                                               static_cast<float>(position[i].y));
                }
                item.hasQuad = true;
                if (!item.text.empty())
                    result.items.push_back(std::move(item));
            }

            result.elapsedMs = ElapsedMs(start);
            result.success = !result.items.empty();
            result.message = result.success ? "OK" : "no barcode decoded";
        }
        catch (const std::exception& e)
        {
            result.elapsedMs = ElapsedMs(start);
            result.success = false;
            result.message = e.what();
        }
        return result;
    }
#else
    BarcodeBenchmarkResult RunZXingCpp(const cv::Mat&, const BarcodeBackendOptions&)
    {
        return MakeDisabled(BarcodeBackendId::ZXingCpp, "ZXing-cpp",
            "未启用: 需要添加 ZXing-cpp 头文件/库和 Apache-2.0 LICENSE");
    }
#endif

    BarcodeBenchmarkResult RunOpenCV(const cv::Mat& image, const BarcodeBackendOptions& backendOptions)
    {
        BarcodeBenchmarkResult result;
        result.backend = BarcodeBackendId::OpenCV;
        result.backendName = "OpenCV QRCodeDetector";
        result.enabled = true;
        result.coordinatePrecision = BarcodeCoordinatePrecision::ModelFloat;

        const auto start = std::chrono::steady_clock::now();
        if ((backendOptions.formatMask & BarcodeFormatQR) == 0)
        {
            result.elapsedMs = ElapsedMs(start);
            result.message = "OpenCV backend supports QR only and QR is filtered out";
            return result;
        }
        if (image.empty())
        {
            result.elapsedMs = ElapsedMs(start);
            result.message = "image is empty";
            return result;
        }

        cv::Mat input;
        if (image.channels() == 1 || image.channels() == 3)
            input = image;
        else if (image.channels() == 4)
            cv::cvtColor(image, input, cv::COLOR_BGRA2BGR);
        else
        {
            result.elapsedMs = ElapsedMs(start);
            result.message = "unsupported image channels";
            return result;
        }

        try
        {
            cv::QRCodeDetector detector;
            std::vector<cv::String> decoded;
            cv::Mat points;
            bool ok = detector.detectAndDecodeMulti(input, decoded, points);
            if (ok)
            {
                for (int i = 0; i < static_cast<int>(decoded.size()); ++i)
                {
                    if (decoded[i].empty())
                        continue;

                    BarcodeBenchmarkItem item;
                    if (BarcodeBenchmark::TryBuildOpenCVItem(
                            decoded[i], points, i, input.size(), item))
                        result.items.push_back(std::move(item));
                }
            }

            if (result.items.empty())
            {
                points.release();
                const std::string text = detector.detectAndDecode(input, points);
                if (!text.empty())
                {
                    BarcodeBenchmarkItem item;
                    if (BarcodeBenchmark::TryBuildOpenCVItem(
                            text, points, 0, input.size(), item))
                        result.items.push_back(std::move(item));
                }
            }
        }
        catch (const cv::Exception& e)
        {
            result.elapsedMs = ElapsedMs(start);
            result.success = false;
            result.message = e.what();
            return result;
        }

        result.elapsedMs = ElapsedMs(start);
        result.success = !result.items.empty();
        result.message = result.success ? "OK" : "no barcode decoded";
        return result;
    }
}

namespace BarcodeBenchmark
{
    bool TryBuildOpenCVItem(
        const std::string& text,
        const cv::Mat& points,
        int index,
        cv::Size imageSize,
        BarcodeBenchmarkItem& item)
    {
        item = {};
        if (text.empty())
            return false;

        const std::vector<cv::Point2f> quad = ReadQuad(points, index);
        if (!IsValidOpenCVQuad(quad, imageSize))
            return false;

        item.text = text;
        item.format = "QR";
        item.bbox = BoundingBoxFromQuad(quad, imageSize);
        std::copy(quad.begin(), quad.end(), item.quad.begin());
        item.hasQuad = true;
        return true;
    }

    BarcodeBenchmarkResult RunBackend(const cv::Mat& image, BarcodeBackendId backend, const BarcodeBackendOptions& options)
    {
        switch (backend)
        {
        case BarcodeBackendId::OpenCV:
            return RunOpenCV(image, options);
        case BarcodeBackendId::ZXingCpp:
            return RunZXingCpp(image, options);
        case BarcodeBackendId::ZBar:
            return MakeDisabled(BarcodeBackendId::ZBar, "ZBar", "未启用: LGPL-2.1，建议仅动态链接并保留 NOTICE");
        case BarcodeBackendId::LibDmtx:
            return MakeDisabled(BarcodeBackendId::LibDmtx, "libdmtx", "未启用: 需要添加 libdmtx 库和对应 LICENSE");
        case BarcodeBackendId::Quirc:
            return MakeDisabled(BarcodeBackendId::Quirc, "quirc", "未启用: 需要添加 quirc 源码和 ISC LICENSE");
        default:
            return MakeDisabled(backend, "Unknown", "unknown barcode backend");
        }
    }

    BarcodeBenchmarkResult RunBackend(const cv::Mat& image, BarcodeBackendId backend)
    {
        return RunBackend(image, backend, BarcodeBackendOptions{});
    }

    std::vector<BarcodeBenchmarkResult> RunBarcodeBenchmark(const cv::Mat& image)
    {
        std::vector<BarcodeBenchmarkResult> results;
        results.reserve(5);
        results.push_back(RunBackend(image, BarcodeBackendId::OpenCV));
        results.push_back(RunBackend(image, BarcodeBackendId::ZXingCpp));
        results.push_back(RunBackend(image, BarcodeBackendId::ZBar));
        results.push_back(RunBackend(image, BarcodeBackendId::LibDmtx));
        results.push_back(RunBackend(image, BarcodeBackendId::Quirc));
        return results;
    }
}
