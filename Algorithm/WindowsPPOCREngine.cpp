#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "WindowsPPOCREngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include <opencv2/imgproc.hpp>
#include <opencv2/geometry/2d.hpp>

#if defined(_WIN32)
#include <Windows.h>
#endif

#if defined(IMGUI_OPENCV_ENABLE_NCNN_OCR)
#if __has_include(<net.h>) && __has_include(<cpu.h>)
#define IMGUI_OPENCV_HAVE_NCNN_OCR 1
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4251 4273)
#endif
#include <cpu.h>
#include <net.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#else
#define IMGUI_OPENCV_HAVE_NCNN_OCR 0
#endif
#else
#define IMGUI_OPENCV_HAVE_NCNN_OCR 0
#endif

namespace
{
namespace fs = std::filesystem;

cv::Size RecognitionCropSize(float rectWidth, float rectHeight, int orientation)
{
    const float rw = std::max(1.0f, rectWidth);
    const float rh = std::max(1.0f, rectHeight);
    const int targetHeight = 48;
    const float widthRatio = rh / rw;
    const int targetWidth = std::clamp(
        static_cast<int>(std::lround(widthRatio * targetHeight)),
        1,
        2048);
    return {targetWidth, targetHeight};
}

bool SameConfig(const WindowsPPOCRConfig& a, const WindowsPPOCRConfig& b)
{
    return a.detParamPath == b.detParamPath &&
        a.detModelPath == b.detModelPath &&
        a.recParamPath == b.recParamPath &&
        a.recModelPath == b.recModelPath &&
        a.dictionaryPath == b.dictionaryPath &&
        a.inputSize == b.inputSize &&
        std::abs(a.minConfidence - b.minConfidence) < 0.0001f &&
        a.maxItems == b.maxItems &&
        a.maxCandidates == b.maxCandidates &&
        a.minBoxArea == b.minBoxArea &&
        a.minBoxHeight == b.minBoxHeight &&
        a.fastMode == b.fastMode &&
        a.detectOnly == b.detectOnly &&
        a.useGPU == b.useGPU;
}

WindowsPPOCRConfig NormalizeConfig(WindowsPPOCRConfig cfg)
{
    cfg.inputSize = std::max(32, cfg.inputSize);
    cfg.maxItems = std::clamp(cfg.maxItems, 1, 1000);
    cfg.maxCandidates = std::clamp(cfg.maxCandidates, 1, 2000);
    cfg.minBoxArea = std::max(0, cfg.minBoxArea);
    cfg.minBoxHeight = std::max(0, cfg.minBoxHeight);
    cfg.minConfidence = std::clamp(cfg.minConfidence, 0.0f, 1.0f);
    return cfg;
}

bool LoadLines(const std::string& path, std::vector<std::string>& lines)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
        return false;

    lines.clear();
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(line);
    }
    return !lines.empty();
}

bool FileExists(const std::string& path)
{
    std::error_code ec;
    const fs::path filePath(path);
    return fs::exists(filePath, ec) && fs::is_regular_file(filePath, ec);
}

fs::path ExecutableDir()
{
#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    while (size == buffer.size())
    {
        buffer.resize(buffer.size() * 2, L'\0');
        size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }
    if (size == 0)
        return {};
    buffer.resize(size);
    return fs::path(buffer).parent_path();
#else
    return fs::current_path();
#endif
}

fs::path ResolvePath(const std::string& path)
{
    const fs::path raw(path);
    if (raw.is_absolute())
        return raw;

    std::vector<fs::path> bases;
    std::error_code ec;
    bases.push_back(fs::current_path(ec));

    const fs::path exeDir = ExecutableDir();
    if (!exeDir.empty())
    {
        for (fs::path dir = exeDir; !dir.empty(); dir = dir.parent_path())
        {
            bases.push_back(dir);
            bases.push_back(dir / "x64" / "Release");
            if (dir == dir.parent_path())
                break;
        }
    }

    for (const fs::path& base : bases)
    {
        if (base.empty())
            continue;
        const fs::path candidate = base / raw;
        if (FileExists(candidate.string()))
            return candidate;
    }

    return raw;
}

std::string ResolvePathString(const std::string& path)
{
    return ResolvePath(path).string();
}

std::string MissingFileMessage(const char* label, const std::string& path)
{
    std::ostringstream out;
    out << "OCR model file missing: " << label << " (" << path << ")";
    return out.str();
}

#if IMGUI_OPENCV_HAVE_NCNN_OCR
struct OCRObject
{
    cv::RotatedRect rrect;
    int orientation = 0;
    float prob = 0.0f;
    std::string text;
    std::vector<float> textScores;
};

double ContourScore(const cv::Mat& binary, const std::vector<cv::Point>& contour)
{
    cv::Rect rect = cv::boundingRect(contour);
    rect &= cv::Rect(0, 0, binary.cols, binary.rows);
    if (rect.empty())
        return 0.0;

    cv::Mat mask = cv::Mat::zeros(rect.height, rect.width, CV_8U);
    std::vector<cv::Point> roiContour;
    roiContour.reserve(contour.size());
    for (const cv::Point& pt : contour)
        roiContour.push_back({pt.x - rect.x, pt.y - rect.y});

    std::vector<std::vector<cv::Point>> roiContours{roiContour};
    cv::fillPoly(mask, roiContours, cv::Scalar(255));
    return cv::mean(binary(rect), mask).val[0] / 255.0;
}

cv::Mat RotateCropImage(const cv::Mat& bgr, const OCRObject& object)
{
    const float rw = std::max(1.0f, object.rrect.size.width);
    const float rh = std::max(1.0f, object.rrect.size.height);
    const cv::Size targetSize = RecognitionCropSize(rw, rh, object.orientation);

    cv::Point2f corners[4];
    object.rrect.points(corners);

    std::vector<cv::Point2f> srcPts(3);
    if (object.orientation == 0)
    {
        srcPts[0] = corners[0];
        srcPts[1] = corners[1];
        srcPts[2] = corners[3];
    }
    else
    {
        srcPts[0] = corners[2];
        srcPts[1] = corners[3];
        srcPts[2] = corners[1];
    }

    const std::vector<cv::Point2f> dstPts{
        {0.0f, 0.0f},
        {static_cast<float>(targetSize.width), 0.0f},
        {0.0f, static_cast<float>(targetSize.height)}
    };

    cv::Mat dst;
    cv::warpAffine(bgr, dst, cv::getAffineTransform(srcPts, dstPts),
        targetSize, cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    return dst;
}

float AverageScore(const OCRObject& object)
{
    if (object.textScores.empty())
        return object.prob;

    float total = 0.0f;
    for (float score : object.textScores)
        total += score;
    return total / static_cast<float>(object.textScores.size());
}

bool HasVisibleText(const std::string& text)
{
    for (unsigned char ch : text)
    {
        if (ch > 0x20)
            return true;
    }
    return false;
}

cv::Rect ObjectBounds(const OCRObject& object)
{
    cv::Point2f corners[4];
    object.rrect.points(corners);

    float left = corners[0].x;
    float top = corners[0].y;
    float right = corners[0].x;
    float bottom = corners[0].y;
    for (int i = 1; i < 4; ++i)
    {
        left = std::min(left, corners[i].x);
        top = std::min(top, corners[i].y);
        right = std::max(right, corners[i].x);
        bottom = std::max(bottom, corners[i].y);
    }

    const int x = static_cast<int>(std::lround(left));
    const int y = static_cast<int>(std::lround(top));
    const int w = std::max(0, static_cast<int>(std::lround(right)) - x);
    const int h = std::max(0, static_cast<int>(std::lround(bottom)) - y);
    return {x, y, w, h};
}

cv::Rect ClampRect(const cv::Rect& rect, const cv::Size& size)
{
    const int left = std::clamp(rect.x, 0, size.width);
    const int top = std::clamp(rect.y, 0, size.height);
    const int right = std::clamp(rect.x + rect.width, left, size.width);
    const int bottom = std::clamp(rect.y + rect.height, top, size.height);
    return {left, top, right - left, bottom - top};
}
#endif

double ElapsedMs(std::chrono::steady_clock::time_point start, std::chrono::steady_clock::time_point end)
{
    return std::chrono::duration<double, std::milli>(end - start).count();
}
} // namespace

struct WindowsPPOCREngine::Impl
{
    WindowsPPOCRConfig config;
    bool ready = false;
    std::vector<std::string> dictionary;
    WindowsPPOCRStats lastStats;

#if IMGUI_OPENCV_HAVE_NCNN_OCR
    ncnn::Net det;
    ncnn::Net rec;
    bool ppocrv6Mode = true;
#endif
};

WindowsPPOCREngine::WindowsPPOCREngine()
    : impl_(std::make_unique<Impl>())
{
}

WindowsPPOCREngine::~WindowsPPOCREngine() = default;

bool WindowsPPOCREngine::Load(const WindowsPPOCRConfig& rawCfg, std::string* error)
{
    if (error)
        error->clear();

    const WindowsPPOCRConfig cfg = NormalizeConfig(rawCfg);

#if !IMGUI_OPENCV_HAVE_NCNN_OCR
    impl_->ready = false;
    impl_->config = cfg;
    if (error)
    {
        *error = "NCNN OCR engine is not enabled in this build. Add Windows ncnn headers/libs and define IMGUI_OPENCV_ENABLE_NCNN_OCR.";
    }
    return false;
#else
    if (impl_->ready && SameConfig(impl_->config, cfg))
        return true;

    const std::string detParamPath = ResolvePathString(cfg.detParamPath);
    const std::string detModelPath = ResolvePathString(cfg.detModelPath);
    const std::string recParamPath = ResolvePathString(cfg.recParamPath);
    const std::string recModelPath = ResolvePathString(cfg.recModelPath);
    const std::string dictionaryPath = ResolvePathString(cfg.dictionaryPath);

    if (!FileExists(detParamPath))
    {
        if (error) *error = MissingFileMessage("det param", cfg.detParamPath);
        return false;
    }
    if (!FileExists(detModelPath))
    {
        if (error) *error = MissingFileMessage("det model", cfg.detModelPath);
        return false;
    }
    if (!FileExists(recParamPath))
    {
        if (error) *error = MissingFileMessage("rec param", cfg.recParamPath);
        return false;
    }
    if (!FileExists(recModelPath))
    {
        if (error) *error = MissingFileMessage("rec model", cfg.recModelPath);
        return false;
    }
    if (!LoadLines(dictionaryPath, impl_->dictionary))
    {
        if (error) *error = MissingFileMessage("dictionary", cfg.dictionaryPath);
        return false;
    }

    impl_->det.clear();
    impl_->rec.clear();
    const int cpuCount = std::max(1, ncnn::get_big_cpu_count());
    impl_->det.opt.num_threads = std::min(cpuCount, 4);
    impl_->det.opt.use_fp16_packed = true;
    impl_->det.opt.use_fp16_storage = true;
    impl_->det.opt.use_fp16_arithmetic = true;
    impl_->rec.opt.num_threads = 1;
    impl_->rec.opt.use_fp16_packed = true;
    impl_->rec.opt.use_fp16_storage = true;
    impl_->rec.opt.use_fp16_arithmetic = true;

#if NCNN_VULKAN
    impl_->det.opt.use_vulkan_compute = cfg.useGPU;
    impl_->rec.opt.use_vulkan_compute = cfg.useGPU;
#endif

    int ret = impl_->det.load_param(detParamPath.c_str());
    if (ret != 0)
    {
        if (error) *error = "NCNN OCR det param load failed";
        return false;
    }
    ret = impl_->det.load_model(detModelPath.c_str());
    if (ret != 0)
    {
        if (error) *error = "NCNN OCR det model load failed";
        return false;
    }
    ret = impl_->rec.load_param(recParamPath.c_str());
    if (ret != 0)
    {
        if (error) *error = "NCNN OCR rec param load failed";
        return false;
    }
    ret = impl_->rec.load_model(recModelPath.c_str());
    if (ret != 0)
    {
        if (error) *error = "NCNN OCR rec model load failed";
        return false;
    }

    impl_->config = cfg;
    impl_->ready = true;
    impl_->ppocrv6Mode = cfg.detParamPath.find("OCRv6") != std::string::npos ||
        cfg.recParamPath.find("OCRv6") != std::string::npos;
    return true;
#endif
}

std::string WindowsPPOCREngine::ResolvePathForTest(const std::string& path)
{
    return ResolvePathString(path);
}

cv::Size WindowsPPOCREngine::RecognitionCropSizeForTest(float rectWidth, float rectHeight, int orientation)
{
    return RecognitionCropSize(rectWidth, rectHeight, orientation);
}

bool WindowsPPOCREngine::Recognize(const cv::Mat& bgr, std::vector<PPOCRTextResult>& out,
    std::string* error, std::stop_token stopToken)
{
    const auto totalStart = std::chrono::steady_clock::now();
    impl_->lastStats = {};
    out.clear();
    if (error)
        error->clear();
    const auto cancelled = [&]()
    {
        if (error)
            *error = "OCR execution cancelled";
        return false;
    };
    if (stopToken.stop_requested())
        return cancelled();

    if (bgr.empty())
    {
        if (error) *error = "OCR input image is empty";
        return false;
    }
    if (!impl_->ready)
    {
        if (error) *error = "NCNN OCR engine is not loaded";
        return false;
    }

#if !IMGUI_OPENCV_HAVE_NCNN_OCR
    if (error)
        *error = "NCNN OCR engine is not enabled in this build";
    return false;
#else
    impl_->lastStats.inputWidth = bgr.cols;
    impl_->lastStats.inputHeight = bgr.rows;

    cv::setNumThreads(std::max(1, ncnn::get_big_cpu_count()));

    const int imgW = bgr.cols;
    const int imgH = bgr.rows;
    const int targetStride = 32;
    int w = imgW;
    int h = imgH;
    float scale = 1.0f;
    if (std::max(w, h) > impl_->config.inputSize)
    {
        if (w > h)
        {
            scale = static_cast<float>(impl_->config.inputSize) / static_cast<float>(w);
            w = impl_->config.inputSize;
            h = static_cast<int>(h * scale);
        }
        else
        {
            scale = static_cast<float>(impl_->config.inputSize) / static_cast<float>(h);
            h = impl_->config.inputSize;
            w = static_cast<int>(w * scale);
        }
    }
    impl_->lastStats.resizedWidth = w;
    impl_->lastStats.resizedHeight = h;

    const auto preprocessStart = std::chrono::steady_clock::now();
    ncnn::Mat in = ncnn::Mat::from_pixels_resize(bgr.data, ncnn::Mat::PIXEL_BGR, imgW, imgH, w, h);
    const int wpad = (w + targetStride - 1) / targetStride * targetStride - w;
    const int hpad = (h + targetStride - 1) / targetStride * targetStride - h;
    ncnn::Mat inPad;
    ncnn::copy_make_border(in, inPad, hpad / 2, hpad - hpad / 2,
        wpad / 2, wpad - wpad / 2, ncnn::BORDER_CONSTANT, 114.0f);

    const float meanVals[3] = {0.485f * 255.0f, 0.456f * 255.0f, 0.406f * 255.0f};
    const float normVals[3] = {1.0f / 0.229f / 255.0f, 1.0f / 0.224f / 255.0f, 1.0f / 0.225f / 255.0f};
    inPad.substract_mean_normalize(meanVals, normVals);
    const auto preprocessEnd = std::chrono::steady_clock::now();
    if (stopToken.stop_requested())
        return cancelled();

    const auto detectStart = std::chrono::steady_clock::now();
    ncnn::Extractor detEx = impl_->det.create_extractor();
    detEx.input(impl_->ppocrv6Mode ? "input" : "in0", inPad);

    ncnn::Mat detOut;
    if (detEx.extract(impl_->ppocrv6Mode ? "output" : "out0", detOut) != 0)
    {
        if (error) *error = "NCNN OCR det output extract failed";
        return false;
    }
    const auto detectEnd = std::chrono::steady_clock::now();
    if (stopToken.stop_requested())
        return cancelled();

    const auto postStart = std::chrono::steady_clock::now();
    const float denormVals[1] = {255.0f};
    detOut.substract_mean_normalize(nullptr, denormVals);

    cv::Mat pred(detOut.h, detOut.w, CV_8UC1);
    detOut.to_pixels(pred.data, ncnn::Mat::PIXEL_GRAY);

    cv::Mat bitmap;
    const float threshold = impl_->ppocrv6Mode ? 0.2f : 0.3f;
    cv::threshold(pred, bitmap, threshold * 255.0f, 255.0, cv::THRESH_BINARY);

    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(bitmap, contours, hierarchy, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    if (contours.size() > 1000)
        contours.resize(1000);
    impl_->lastStats.contours = static_cast<int>(contours.size());

    std::vector<OCRObject> objects;
    const float boxThresh = impl_->ppocrv6Mode ? 0.45f : 0.60f;
    const float enlargeRatio = impl_->ppocrv6Mode ? 1.40f : 1.95f;
    const float minSize = 3.0f * scale;

    for (const auto& contour : contours)
    {
        if (stopToken.stop_requested())
            return cancelled();
        if (contour.size() <= 2)
            continue;
        const double score = ContourScore(pred, contour);
        if (score < boxThresh)
            continue;

        cv::RotatedRect rrect = cv::minAreaRect(contour);
        if (std::max(rrect.size.width, rrect.size.height) < minSize)
            continue;
        const cv::Rect sourceBounds = cv::boundingRect(contour);
        const float boxW = sourceBounds.width / scale;
        const float boxH = sourceBounds.height / scale;
        if (impl_->config.minBoxArea > 0 && boxW * boxH < static_cast<float>(impl_->config.minBoxArea))
            continue;
        if (impl_->config.minBoxHeight > 0 && std::max(boxW, boxH) < static_cast<float>(impl_->config.minBoxHeight))
            continue;

        int orientation = 0;
        if (rrect.angle >= -30 && rrect.angle <= 30 && rrect.size.height > rrect.size.width * 2.7f)
            orientation = 1;
        if ((rrect.angle <= -60 || rrect.angle >= 60) && rrect.size.width > rrect.size.height * 2.7f)
            orientation = 1;

        if (rrect.angle < -30)
            rrect.angle += 180;
        if (orientation == 0 && rrect.angle < 30)
        {
            rrect.angle += 90;
            std::swap(rrect.size.width, rrect.size.height);
        }
        if (orientation == 1 && rrect.angle >= 60)
        {
            rrect.angle -= 90;
            std::swap(rrect.size.width, rrect.size.height);
        }

        rrect.size.height += rrect.size.width * (enlargeRatio - 1.0f);
        rrect.size.width *= enlargeRatio;
        rrect.center.x = (rrect.center.x - (wpad / 2.0f)) / scale;
        rrect.center.y = (rrect.center.y - (hpad / 2.0f)) / scale;
        rrect.size.width /= scale;
        rrect.size.height /= scale;

        OCRObject object;
        object.rrect = rrect;
        object.orientation = orientation;
        object.prob = static_cast<float>(score);
        objects.push_back(object);
    }

    std::sort(objects.begin(), objects.end(), [](const OCRObject& a, const OCRObject& b) {
        if (a.prob != b.prob)
            return a.prob > b.prob;
        return a.rrect.size.area() > b.rrect.size.area();
    });

    const size_t requestedItems = static_cast<size_t>(impl_->config.maxItems);
    const size_t candidateSlack = impl_->config.fastMode
        ? std::clamp(requestedItems / 8, static_cast<size_t>(8), static_cast<size_t>(24))
        : std::clamp(requestedItems / 4, static_cast<size_t>(16), static_cast<size_t>(48));
    const size_t configuredCandidateLimit = static_cast<size_t>(impl_->config.maxCandidates);
    const size_t candidateLimit = std::min({objects.size(), requestedItems + candidateSlack, configuredCandidateLimit});
    if (objects.size() > candidateLimit)
        objects.resize(candidateLimit);
    impl_->lastStats.candidates = static_cast<int>(objects.size());
    const auto postEnd = std::chrono::steady_clock::now();

    if (impl_->config.detectOnly)
    {
        std::vector<OCRObject> readable = objects;
        if (readable.size() > static_cast<size_t>(impl_->config.maxItems))
            readable.resize(static_cast<size_t>(impl_->config.maxItems));
        impl_->lastStats.workers = 0;
        impl_->lastStats.recognizedCandidates = 0;
        impl_->lastStats.readableTexts = static_cast<int>(readable.size());
        out.reserve(readable.size());
        for (const OCRObject& object : readable)
        {
            if (stopToken.stop_requested())
                return cancelled();
            PPOCRTextResult item;
            item.text = "det";
            item.confidence = object.prob;
            item.box = ClampRect(ObjectBounds(object), bgr.size());
            out.push_back(item);
        }
        const auto totalEnd = std::chrono::steady_clock::now();
        impl_->lastStats.preprocessMs = ElapsedMs(preprocessStart, preprocessEnd);
        impl_->lastStats.detectMs = ElapsedMs(detectStart, detectEnd);
        impl_->lastStats.postprocessMs = ElapsedMs(postStart, postEnd);
        impl_->lastStats.recognizeMs = 0.0;
        impl_->lastStats.totalMs = ElapsedMs(totalStart, totalEnd);
        return true;
    }

    auto recognizeOne = [&](OCRObject& object)
    {
        if (stopToken.stop_requested())
            return;
        cv::Mat roi = RotateCropImage(bgr, object);
        if (roi.empty())
            return;

        ncnn::Mat recIn = ncnn::Mat::from_pixels(roi.data, ncnn::Mat::PIXEL_BGR, roi.cols, roi.rows);
        const float recMean[3] = {127.5f, 127.5f, 127.5f};
        const float recNorm[3] = {1.0f / 127.5f, 1.0f / 127.5f, 1.0f / 127.5f};
        recIn.substract_mean_normalize(recMean, recNorm);

        ncnn::Extractor recEx = impl_->rec.create_extractor();
        recEx.input(impl_->ppocrv6Mode ? "input" : "in0", recIn);
        ncnn::Mat recOut;
        if (recEx.extract(impl_->ppocrv6Mode ? "output" : "out0", recOut) != 0)
            return;
        if (stopToken.stop_requested())
            return;

        int lastToken = 0;
        for (int i = 0; i < recOut.h; ++i)
        {
            if (stopToken.stop_requested())
                return;
            const float* p = recOut.row(i);
            int index = 0;
            float maxScore = -9999.0f;
            for (int j = 0; j < recOut.w; ++j)
            {
                const float score = *p++;
                if (score > maxScore)
                {
                    maxScore = score;
                    index = j;
                }
            }

            if (lastToken == index)
                continue;
            lastToken = index;
            if (index <= 0)
                continue;
            if (index < static_cast<int>(impl_->dictionary.size()))
            {
                object.text += impl_->dictionary[index];
                object.textScores.push_back(maxScore);
            }
        }
    };

    const int cpuCount = std::max(1, ncnn::get_big_cpu_count());
    const int workerLimit = objects.size() >= 64 ? 8 : 4;
    const int workerCount = std::min<int>(std::max(1, std::min(cpuCount, workerLimit)), static_cast<int>(objects.size()));
    impl_->lastStats.workers = workerCount;
    const auto recognizeStart = std::chrono::steady_clock::now();
    if (workerCount <= 1)
    {
        for (OCRObject& object : objects)
        {
            if (stopToken.stop_requested())
                break;
            recognizeOne(object);
        }
    }
    else
    {
        std::vector<std::thread> workers;
        workers.reserve(static_cast<size_t>(workerCount));
        for (int worker = 0; worker < workerCount; ++worker)
        {
            workers.emplace_back([&, worker]() {
                for (size_t i = static_cast<size_t>(worker); i < objects.size(); i += static_cast<size_t>(workerCount))
                {
                    if (stopToken.stop_requested())
                        break;
                    recognizeOne(objects[i]);
                }
            });
        }
        for (std::thread& worker : workers)
            worker.join();
    }
    if (stopToken.stop_requested())
        return cancelled();
    const auto recognizeEnd = std::chrono::steady_clock::now();
    impl_->lastStats.recognizedCandidates = static_cast<int>(objects.size());

    std::vector<OCRObject> readable;
    readable.reserve(objects.size());
    for (const OCRObject& object : objects)
    {
        if (stopToken.stop_requested())
            return cancelled();
        if (HasVisibleText(object.text) && AverageScore(object) >= impl_->config.minConfidence)
            readable.push_back(object);
    }

    std::sort(readable.begin(), readable.end(), [](const OCRObject& a, const OCRObject& b) {
        const cv::Rect ar = ObjectBounds(a);
        const cv::Rect br = ObjectBounds(b);
        if (std::abs(ar.y - br.y) > 18)
            return ar.y < br.y;
        return ar.x < br.x;
    });
    if (readable.size() > static_cast<size_t>(impl_->config.maxItems))
        readable.resize(static_cast<size_t>(impl_->config.maxItems));
    impl_->lastStats.readableTexts = static_cast<int>(readable.size());

    out.reserve(readable.size());
    for (const OCRObject& object : readable)
    {
        if (stopToken.stop_requested())
            return cancelled();
        PPOCRTextResult item;
        item.text = object.text;
        item.confidence = AverageScore(object);
        item.box = ClampRect(ObjectBounds(object), bgr.size());
        out.push_back(item);
    }
    const auto totalEnd = std::chrono::steady_clock::now();
    impl_->lastStats.preprocessMs = ElapsedMs(preprocessStart, preprocessEnd);
    impl_->lastStats.detectMs = ElapsedMs(detectStart, detectEnd);
    impl_->lastStats.postprocessMs = ElapsedMs(postStart, postEnd);
    impl_->lastStats.recognizeMs = ElapsedMs(recognizeStart, recognizeEnd);
    impl_->lastStats.totalMs = ElapsedMs(totalStart, totalEnd);
    return true;
#endif
}

bool WindowsPPOCREngine::IsReady() const
{
    return impl_->ready;
}

WindowsPPOCRStats WindowsPPOCREngine::LastStats() const
{
    return impl_->lastStats;
}
