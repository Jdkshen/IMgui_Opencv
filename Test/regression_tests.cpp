#include "../Algorithm/BlobTool.h"
#include "../Algorithm/ColorAnalyzer.h"
#include "../Algorithm/EdgeTool.h"
#include "../Algorithm/MorphologyTool.h"
#include "../Algorithm/CaliperOperators.h"
#include "../Algorithm/MeasurementTool.h"
#include "../Algorithm/ThresholdTool.h"
#include "../Algorithm/TemplateMatchingTool.h"
#include "../Algorithm/YOLOTool.h"
#include "../Algorithm/ShapeMatcher.h"
#include "../Algorithm/ShapeTools.h"
#include "../Algorithm/MultiColorFinder.h"
#include "../Algorithm/OCRTool.h"
#include "../Algorithm/QRCodeTool.h"
#include "../Algorithm/WindowsPPOCREngine.h"
#include "../Core/RecipeManager.h"
#include "../Core/CalibrationModel.h"
#include "../Core/FrameSourceState.h"
#include "../Core/FixtureTransform.h"
#include "../Core/ImageState.h"
#include "../Core/OpenFileDialog.h"
#include "../Core/ResultOverlayState.h"
#include "../Core/ResultROIResolver.h"
#include "../Core/ResultExporter.h"
#include "../Core/ROIState.h"
#include "../Core/ToolChainState.h"
#include "../Core/ToolExecutor.h"
#include "../Core/ToolJudgement.h"
#include "../Core/VisionContext.h"
#include "../UI/ROIManager.h"
#include "../UI/ToolsWindow.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

extern cv::Mat& gImage;
extern cv::Mat& gOriginalImage;
extern int& gImageWidth;
extern int& gImageHeight;
extern int& g_ImageVersion;

namespace
{
struct TestDisposableTool final : ITool
{
    explicit TestDisposableTool(bool* destroyedFlag) : destroyed(destroyedFlag) {}
    ~TestDisposableTool() override
    {
        if (destroyed)
            *destroyed = true;
    }

    const char* GetName() const override { return "test"; }
    int GetType() const override { return 99; }
    ToolResult Execute(VisionContext&) override { return {}; }
    void DrawUI() override {}
    nlohmann::json Save() const override { return {}; }
    void Load(const nlohmann::json&) override {}

    bool* destroyed = nullptr;
};

void Require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::filesystem::path FindRepoRoot()
{
    std::filesystem::path dir = std::filesystem::current_path();
    for (int i = 0; i < 8; ++i) {
        if (std::filesystem::exists(dir / "Windows_imgui.slnx") &&
            std::filesystem::exists(dir / "assets" / "images" / "test.jpg")) {
            return dir;
        }
        if (!dir.has_parent_path())
            break;
        dir = dir.parent_path();
    }
    throw std::runtime_error("repo root not found");
}

cv::Mat DrawToolResultOverlay(const cv::Mat& image, const ToolResult& result)
{
    cv::Mat out;
    if (image.channels() == 1)
        cv::cvtColor(image, out, cv::COLOR_GRAY2BGR);
    else
        out = image.clone();

    for (const auto& region : result.regions) {
        cv::rectangle(out, region.bbox, cv::Scalar(0, 255, 0), 2);
    }
    for (const auto& detection : result.detections) {
        cv::rectangle(out, detection.box, cv::Scalar(255, 0, 0), 2);
    }
    for (const auto& line : result.lines) {
        cv::line(out, line.p1, line.p2, cv::Scalar(0, 255, 255), 2);
    }
    return out;
}

void TestTemplateMatch()
{
    cv::Mat image = cv::Mat::zeros(80, 100, CV_8UC1);
    cv::rectangle(image, cv::Rect(40, 25, 12, 10), cv::Scalar(255), cv::FILLED);
    cv::Mat templ = image(cv::Rect(40, 25, 12, 10)).clone();

    cv::Mat result;
    cv::matchTemplate(image, templ, result, cv::TM_SQDIFF_NORMED);

    double minVal = 0.0;
    cv::Point minLoc;
    cv::minMaxLoc(result, &minVal, nullptr, &minLoc, nullptr);

    Require(minVal <= 1e-6, "template match score regressed");
    Require(std::abs(minLoc.x - 40) <= 1 && std::abs(minLoc.y - 25) <= 1,
        "template match location regressed");
}

void TestRoiConversion()
{
    VisionContext ctx;
    ROI roi;
    roi.start = ImVec2(30.0f, 40.0f);
    roi.end = ImVec2(10.0f, 15.0f);
    ctx.rois.push_back(roi);
    ctx.selectedROI = 0;

    cv::Rect rect = ctx.GetActiveROIRect();
    Require(rect.x == 10 && rect.y == 15 && rect.width == 20 && rect.height == 25,
        "ROI coordinate conversion regressed");
}

void TestYoloToolNoModelPath()
{
    VisionContext ctx;
    ctx.image = cv::Mat::zeros(32, 32, CV_8UC3);

    YOLOTool tool;
    ToolResult result = tool.Execute(ctx);

    Require(!result.success, "YOLO should fail when no model is loaded");
    Require(result.message == "model is not loaded", "YOLO failure message regressed");
}

void TestQRCodeToolRecognizesBundledSample()
{
    const std::filesystem::path imagePath =
        FindRepoRoot() / "assets" / "images" / "qr_tests" / "qr_test.png";
    cv::Mat image = cv::imread(imagePath.string(), cv::IMREAD_COLOR);
    Require(!image.empty(), "QR sample image load failed");

    VisionContext ctx;
    ctx.image = image;
    ctx.originalImage = image;
    ctx.width = image.cols;
    ctx.height = image.rows;

    QRCodeTool tool;
    tool.useROI = false;
    tool.engine = 2;
    tool.minSize = 1;
    ToolResult result = tool.Execute(ctx);

    Require(result.success, result.message.c_str());
    Require(!result.texts.empty(), "QR sample produced no decoded text");
    Require(!result.texts.front().text.empty(), "QR sample decoded empty text");
    std::cout << "qr_code_tool: decoded " << result.texts.size()
              << " item(s) with " << result.message << "\n";
    std::unique_ptr<ITool> factoryTool = ITool::Create(14);
    Require(factoryTool != nullptr && factoryTool->GetType() == 14,
        "QR tool factory registration regressed");
}

void TestRecursiveImageFolderScanSupportsCommonFormats()
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "imgui_opencv_image_scan_regression";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "nested" / "deep");

    std::ofstream(root / "root.JPG").put('\0');
    std::ofstream(root / "ignored.txt").put('\0');
    std::ofstream(root / "nested" / "image.jpeg").put('\0');
    std::ofstream(root / "nested" / "image.tif").put('\0');
    std::ofstream(root / "nested" / "deep" / "image.tiff").put('\0');
    std::ofstream(root / "nested" / "deep" / "image.webp").put('\0');

    const std::vector<std::string> flat = ScanImageFiles(root.string(), false);
    const std::vector<std::string> recursive = ScanImageFiles(root.string(), true);
    Require(flat.size() == 1, "flat image scan should only include root files");
    Require(recursive.size() == 5, "recursive image scan format support regressed");

    std::filesystem::remove_all(root);
}

void TestToolJudgementPolicy()
{
    ToolJudgementSettings settings;
    settings.enabled = true;
    settings.stopOnFailure = true;
    settings.minResultCount = 2;
    settings.minScore = 0.8f;
    settings.minArea = 100.0f;
    settings.maxArea = 1000.0f;
    settings.requiredText = "PASS";
    settings.textMatchMode = 0;
    settings.textCaseSensitive = false;

    ToolResult result;
    result.success = true;
    result.texts.push_back({"station pass", cv::Rect(0, 0, 20, 20), 0.9f});
    result.texts.push_back({"station pass 2", cv::Rect(20, 0, 20, 20), 0.85f});
    ToolJudgement::Evaluate(result, settings);
    Require(result.status == ToolResultStatus::Pass, "matching judgement should pass");
    Require(!ToolJudgement::ShouldStop(result, settings), "pass result should not stop chain");

    result.texts.resize(1);
    ToolJudgement::Evaluate(result, settings);
    Require(result.status == ToolResultStatus::Fail, "result count judgement should fail");
    Require(ToolJudgement::ShouldStop(result, settings), "configured fail should stop chain");

    result.success = false;
    result.message = "backend unavailable";
    ToolJudgement::Evaluate(result, settings);
    Require(result.status == ToolResultStatus::Error, "execution error status regressed");
    Require(result.statusReason == result.message, "error reason propagation regressed");
}

void TestIndustrialMeasurement()
{
    VisionContext context;
    ROI line;
    line.type = ROI_TYPE_LINE;
    line.start = ImVec2(0.0f, 0.0f);
    line.end = ImVec2(30.0f, 40.0f);
    context.rois.push_back(line);

    MeasurementTool tool;
    tool.mode = 0;
    tool.mmPerPixel = 0.1f;
    tool.toleranceEnabled = true;
    tool.nominal = 4.0f;
    tool.toleranceMinus = 0.2f;
    tool.tolerancePlus = 0.2f;

    ToolResult result = tool.Execute(context);
    Require(result.success, "distance measurement failed");
    Require(!result.measurements.empty() && std::abs(result.measurements[0].value - 5.0) < 0.001,
        "pixel to millimeter calibration regressed");
    ToolJudgement::Evaluate(result, ToolJudgementSettings{});
    Require(result.status == ToolResultStatus::Fail, "measurement tolerance NG was lost");

    tool.toleranceEnabled = false;
    result = tool.Execute(context);
    ToolJudgementSettings settings;
    settings.enabled = true;
    settings.minResultCount = 1;
    ToolJudgement::Evaluate(result, settings);
    Require(result.status == ToolResultStatus::Pass,
        "measurement output was not counted by common judgement");

    VisionContext caliperContext;
    caliperContext.image = cv::Mat(100, 140, CV_8UC1, cv::Scalar(20));
    caliperContext.image.rowRange(35, 66).setTo(cv::Scalar(220));
    ROI caliperROI;
    caliperROI.type = ROI_TYPE_RECT;
    caliperROI.start = {10.0f, 15.0f};
    caliperROI.end = {130.0f, 85.0f};
    caliperContext.rois.push_back(caliperROI);

    MeasurementTool widthTool;
    widthTool.mode = 1;
    widthTool.caliperCount = 12;
    widthTool.caliper.edgeThreshold = 15.0f;
    widthTool.caliper.polarity = CaliperOperators::EdgePolarity::DarkToBright;
    widthTool.minimumValidCalipers = 10;
    ToolResult widthResult = widthTool.Execute(caliperContext);
    Require(widthResult.success && widthResult.status == ToolResultStatus::Pass,
        "industrial edge-pair width measurement failed");
    Require(!widthResult.measurements.empty() &&
        std::abs(widthResult.measurements.front().value - 31.0) < 1.0,
        "industrial edge-pair width value regressed");
    bool hasConfidence = false;
    for (const auto& measurement : widthResult.measurements)
        hasConfidence |= measurement.name == "confidence" && measurement.value > 0.5;
    Require(hasConfidence, "industrial measurement quality metrics were not published");
}

void TestCaliperOperators()
{
    using namespace CaliperOperators;

    cv::Mat rising(80, 120, CV_8UC1, cv::Scalar(20));
    rising.colRange(51, rising.cols).setTo(cv::Scalar(220));
    CaliperParams params;
    params.searchLength = 60.0f;
    params.projectionWidth = 9.0f;
    params.smoothingSigma = 1.0f;
    params.edgeThreshold = 15.0f;
    params.polarity = EdgePolarity::DarkToBright;
    EdgePoint risingEdge = FindEdge(rising, {50.0f, 40.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, params);
    Require(risingEdge.valid, "dark-to-bright caliper edge was not found");
    Require(std::abs(risingEdge.position.x - 50.5f) < 0.75f,
        "dark-to-bright subpixel edge position regressed");

    params.polarity = EdgePolarity::BrightToDark;
    EdgePoint rejected = FindEdge(rising, {50.0f, 40.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, params);
    Require(!rejected.valid, "caliper polarity filter accepted the wrong edge");

    cv::Mat band(80, 120, CV_8UC1, cv::Scalar(20));
    band.colRange(35, 76).setTo(cv::Scalar(220));
    params.polarity = EdgePolarity::DarkToBright;
    params.searchLength = 80.0f;
    EdgePair pair = FindEdgePair(band, {55.0f, 40.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, params);
    Require(pair.valid, "edge-pair caliper did not find both edges");
    Require(std::abs(pair.distance - 41.0f) < 1.0f, "edge-pair width regressed");

    std::vector<cv::Point2f> linePoints;
    for (int x = 0; x < 20; ++x)
        linePoints.emplace_back(static_cast<float>(x), 2.0f * x + 3.0f);
    linePoints.emplace_back(5.0f, 80.0f);
    FittedLine line = FitLine(linePoints, FitMethod::Ransac, 0.5f);
    Require(line.valid && line.inliers.size() == 20, "RANSAC line inlier selection regressed");
    Require(line.quality.maxError < 0.1f, "RANSAC line residual regressed");

    std::vector<cv::Point2f> circlePoints;
    for (int i = 0; i < 24; ++i) {
        const float angle = static_cast<float>(2.0 * 3.14159265358979323846 * i / 24.0);
        circlePoints.emplace_back(40.0f + 15.0f * std::cos(angle), 30.0f + 15.0f * std::sin(angle));
    }
    circlePoints.emplace_back(100.0f, 100.0f);
    FittedCircle circle = FitCircle(circlePoints, FitMethod::Ransac, 0.5f);
    Require(circle.valid && circle.inliers.size() == 24, "RANSAC circle inlier selection regressed");
    Require(cv::norm(circle.center - cv::Point2f(40.0f, 30.0f)) < 0.1f &&
            std::abs(circle.radius - 15.0f) < 0.1f,
        "RANSAC circle fit regressed");
}

void TestCalibrationModel()
{
    CalibrationModel scale;
    scale.enabled = true;
    scale.scaleX = 0.1;
    scale.scaleY = 0.2;
    scale.pixelOrigin = {10.0, 20.0};
    scale.worldOrigin = {1.0, 2.0};
    const cv::Point2d scaled = scale.PixelToWorld({20.0, 30.0});
    Require(cv::norm(scaled - cv::Point2d(2.0, 4.0)) < 1.0e-9,
        "independent X/Y calibration scale regressed");

    CalibrationModel perspective;
    perspective.enabled = true;
    perspective.homographyEnabled = true;
    perspective.pixelToWorldHomography = cv::Matx33d(
        0.5, 0.0, 10.0,
        0.0, 0.25, 20.0,
        0.0, 0.0, 1.0);
    const cv::Point2d transformed = perspective.PixelToWorld({20.0, 40.0});
    Require(cv::norm(transformed - cv::Point2d(20.0, 30.0)) < 1.0e-9,
        "homography pixel-to-world conversion regressed");

    CalibrationModel distortion;
    distortion.distortionEnabled = true;
    distortion.fx = 800.0;
    distortion.fy = 800.0;
    distortion.cx = 320.0;
    distortion.cy = 240.0;
    const cv::Point2d unchanged = distortion.UndistortPixel({100.0, 120.0});
    Require(cv::norm(unchanged - cv::Point2d(100.0, 120.0)) < 1.0e-6,
        "zero lens distortion should preserve pixel coordinates");
}

void TestFixtureTransform()
{
    ToolResult result;
    ToolResult::Region region;
    region.bbox = cv::Rect(90, 40, 20, 20);
    region.angle = 90.0f;
    result.regions.push_back(region);

    FixturePose current;
    Require(FixtureTransform::TryExtractPose(result, 0, current),
        "fixture pose extraction from region failed");
    FixturePose reference;
    reference.valid = true;
    reference.origin = {50.0f, 50.0f};
    reference.angleDegrees = 0.0f;

    const cv::Point2f transformed = FixtureTransform::TransformPoint({60.0f, 50.0f}, reference, current);
    Require(cv::norm(transformed - cv::Point2f(100.0f, 60.0f)) < 0.001f,
        "fixture rigid point transform regressed");

    ROI rectangle;
    rectangle.type = ROI_TYPE_RECT;
    rectangle.start = {55.0f, 45.0f};
    rectangle.end = {65.0f, 55.0f};
    const ROI transformedROI = FixtureTransform::TransformROI(rectangle, reference, current);
    Require(transformedROI.type == ROI_TYPE_POLYGON && transformedROI.points.size() == 4,
        "rotated fixture rectangle should become a polygon ROI");
}

void TestResultROIResolution()
{
    ToolResult source;
    source.detections.push_back({cv::Rect(10, 20, 30, 40), 0, 0.9f, "A"});
    source.detections.push_back({cv::Rect(50, 60, 20, 10), 1, 0.8f, "B"});

    ResultROIRequest request;
    request.mode = ResultROIMode::NthResult;
    request.resultIndex = 1;
    ResultROIResolution resolution = ResultROIResolver::Resolve(source, request, cv::Size(100, 100));
    Require(resolution.available && resolution.rois.size() == 1,
        "Nth result ROI resolution failed");
    Require(resolution.rois[0].ToCvRect() == cv::Rect(50, 60, 20, 10),
        "Nth result ROI geometry regressed");

    request.mode = ResultROIMode::AllResults;
    resolution = ResultROIResolver::Resolve(source, request, cv::Size(100, 100));
    Require(resolution.available && resolution.rois.size() == 2,
        "all result ROI resolution failed");

    request.resultIndex = 9;
    request.mode = ResultROIMode::NthResult;
    resolution = ResultROIResolver::Resolve(source, request);
    Require(!resolution.available && !resolution.reason.empty(),
        "missing result ROI policy input was not reported");
}

void TestTemplateMatchingToolUsesInstanceParameters()
{
    VisionContext context;
    context.image = cv::Mat::zeros(120, 160, CV_8UC1);
    cv::rectangle(context.image, cv::Rect(55, 35, 24, 18), cv::Scalar(180), cv::FILLED);
    cv::line(context.image, cv::Point(55, 35), cv::Point(78, 52), cv::Scalar(255), 2);

    TemplateMatchingTool tool;
    tool.templateImg = context.image(cv::Rect(55, 35, 24, 18)).clone();
    tool.matchThreshold = 0.95f;
    tool.maxResults = 1;
    tool.maxImageDim = 1000;

    ToolResult result = tool.Execute(context);
    Require(result.success && result.regions.size() == 1,
        "instance-based template matching failed");
    Require(std::abs(result.regions[0].bbox.x - 55) <= 1 &&
        std::abs(result.regions[0].bbox.y - 35) <= 1,
        "template matching coordinates regressed");
}

void TestToolChainReorderRemapsResultROISource()
{
    auto savedTools = std::move(ToolChainState::Tools());
    const int savedActive = ToolChainState::ActiveIndex();
    const int savedLive = ToolChainState::YoloLiveInstanceIndex();

    auto& tools = ToolChainState::Tools();
    tools.clear();
    ToolInstance detector;
    detector.type = 4;
    ToolInstance consumer;
    consumer.type = 2;
    consumer.resultRoiMode = 1;
    consumer.resultRoiSourceTool = 0;
    consumer.fixture.enabled = true;
    consumer.fixture.sourceToolIndex = 0;
    ToolInstance original;
    original.type = 12;
    tools.push_back(std::move(detector));
    tools.push_back(std::move(consumer));
    tools.push_back(std::move(original));
    ToolChainState::SetActiveIndex(1);
    ToolChainState::SetYoloLiveInstanceIndex(0);

    ToolChainState::MoveOriginalToolToFront();
    Require(tools[0].type == 12 && tools[2].type == 2,
        "original tool reorder regressed");
    Require(tools[2].resultRoiSourceTool == 1,
        "result ROI source index was not remapped after reorder");
    Require(tools[2].fixture.sourceToolIndex == 1,
        "fixture source index was not remapped after reorder");
    Require(ToolChainState::ActiveIndex() == 2 && ToolChainState::YoloLiveInstanceIndex() == 1,
        "runtime tool indices were not remapped after reorder");

    tools = std::move(savedTools);
    ToolChainState::SetActiveIndex(savedActive);
    ToolChainState::SetYoloLiveInstanceIndex(savedLive);
}

void TestRecipeRoundTrip()
{
    RecipeData data;
    data.name = "regression";
    data.imagePath = "assets/images/test.jpg";
    data.threshold.useGray = true;
    data.threshold.thresholdValue = 123;
    data.tmMatch.maxResults = 3;
    data.tmMatch.matchThreshold = 0.91f;
    data.rois.push_back({1.0f, 2.0f, 30.0f, 40.0f, 0});

    RecipeToolInstance tool;
    tool.type = 4;
    tool.label = "定位A";
    tool.yoloModelPath = "models/yolo.onnx";
    tool.yoloClassesPath = "models/classes.txt";
    tool.yoloConfThreshold = 0.67f;
    tool.yoloNmsThreshold = 0.45f;
    tool.yoloUseROI = true;
    tool.judgement.enabled = true;
    tool.judgement.stopOnFailure = true;
    tool.judgement.minResultCount = 2;
    tool.judgement.maxResultCount = 5;
    tool.judgement.minScore = 0.75f;
    tool.judgement.requiredText = "target";
    tool.resultRoiMode = 1;
    tool.resultRoiSourceTool = 0;
    tool.resultRoiIndex = 2;
    tool.resultRoiMissingPolicy = 1;
    tool.searchROIs.push_back({5.0f, 6.0f, 20.0f, 21.0f, 0});
    data.tools.push_back(tool);

    RecipeToolInstance mcf;
    mcf.type = 10;
    mcf.mcfUseROI = true;
    mcf.mcfMaxResults = 7;
    mcf.mcfMinDist = 11.5f;
    mcf.mcfCrossSize = 13;
    mcf.mcfCrossThick = 4;
    mcf.mcfAnchorX = 21;
    mcf.mcfAnchorY = 22;
    mcf.mcfImgGray = true;
    mcf.mcfImgBinary = true;
    mcf.mcfImgBinThresh = 173;
    mcf.mcfRoiX = 2;
    mcf.mcfRoiY = 3;
    mcf.mcfRoiW = 40;
    mcf.mcfRoiH = 41;
    mcf.mcfRefImageBase64.assign("\x89PNG\r\n\x1a\n\xff", 9);
    mcf.mcfPointsJson = R"({"points":[{"x":1,"y":2,"b":3,"g":4,"r":5,"tolerance":6}]})";
    data.tools.push_back(mcf);

    RecipeToolInstance qr;
    qr.type = 14;
    qr.qrUseROI = false;
    qr.qrDetectMulti = false;
    qr.qrEnhance = false;
    qr.qrMinSize = 37;
    qr.qrShowText = false;
    qr.qrEngine = 2;
    qr.qrFormatMask = BarcodeFormatCode128 | BarcodeFormatDataMatrix;
    qr.qrFilterDuplicates = false;
    data.tools.push_back(qr);

    RecipeToolInstance measurement;
    measurement.type = 15;
    measurement.measureMode = 3;
    measurement.measureCaliperCount = 24;
    measurement.measureSearchLength = 18.0f;
    measurement.measureProjectionWidth = 7.0f;
    measurement.measureEdgePolarity = 2;
    measurement.measureSubpixel = true;
    measurement.measureFitMethod = 1;
    measurement.measureFitInlierThreshold = 0.8f;
    measurement.measureMinimumValidCalipers = 12;
    measurement.measureMinimumConfidence = 0.75f;
    measurement.measureMmPerPixel = 0.025f;
    measurement.measureCalibration.enabled = true;
    measurement.measureCalibration.scaleX = 0.02;
    measurement.measureCalibration.scaleY = 0.03;
    measurement.measureCalibration.homographyEnabled = true;
    measurement.measureCalibration.pixelToWorldHomography(0, 2) = 4.5;
    measurement.measureCalibration.distortionEnabled = true;
    measurement.measureCalibration.fx = 800.0;
    measurement.fixture.enabled = true;
    measurement.fixture.sourceToolIndex = 0;
    measurement.fixture.referenceOrigin = {12.0f, 34.0f};
    measurement.fixture.referenceAngleDegrees = 5.0f;
    measurement.measureToleranceEnabled = true;
    measurement.measureNominal = 12.0f;
    measurement.measureToleranceMinus = 0.1f;
    measurement.measureTolerancePlus = 0.2f;
    data.tools.push_back(measurement);

    std::filesystem::path path = std::filesystem::temp_directory_path() / "imgui_opencv_regression.recipe";
    std::filesystem::remove(path);

    Require(RecipeManager::Save(path.string().c_str(), data), "recipe save failed");

    RecipeData loaded;
    Require(RecipeManager::Load(path.string().c_str(), loaded), "recipe load failed");

    Require(loaded.name == data.name, "recipe name round-trip regressed");
    Require(loaded.threshold.useGray == data.threshold.useGray, "threshold round-trip regressed");
    Require(loaded.threshold.thresholdValue == data.threshold.thresholdValue, "threshold value round-trip regressed");
    Require(loaded.rois.size() == 1 && loaded.rois[0].endX == 30.0f, "ROI round-trip regressed");
    Require(loaded.tools.size() == 4, "tool count round-trip regressed");
    Require(loaded.tools[0].type == 4, "YOLO tool type round-trip regressed");
    Require(loaded.tools[0].label == "定位A", "tool label round-trip regressed");
    Require(loaded.tools[0].yoloUseROI, "YOLO ROI flag round-trip regressed");
    Require(loaded.tools[0].judgement.enabled && loaded.tools[0].judgement.stopOnFailure,
        "judgement flags round-trip regressed");
    Require(loaded.tools[0].judgement.minResultCount == 2 && loaded.tools[0].judgement.maxResultCount == 5,
        "judgement count round-trip regressed");
    Require(std::abs(loaded.tools[0].judgement.minScore - 0.75f) < 0.001f &&
        loaded.tools[0].judgement.requiredText == "target",
        "judgement conditions round-trip regressed");
    Require(std::abs(loaded.tools[0].yoloConfThreshold - 0.67f) < 0.001f,
        "YOLO confidence round-trip regressed");
    Require(loaded.tools[0].resultRoiMode == 1 && loaded.tools[0].resultRoiSourceTool == 0 &&
        loaded.tools[0].resultRoiIndex == 2 && loaded.tools[0].resultRoiMissingPolicy == 1,
        "result ROI settings round-trip regressed");
    Require(loaded.tools[1].type == 10, "multi-color tool type round-trip regressed");
    Require(loaded.tools[1].mcfUseROI, "multi-color ROI flag round-trip regressed");
    Require(loaded.tools[1].mcfMaxResults == 7, "multi-color max results round-trip regressed");
    Require(std::abs(loaded.tools[1].mcfMinDist - 11.5f) < 0.001f, "multi-color min distance round-trip regressed");
    Require(loaded.tools[1].mcfCrossSize == 13, "multi-color cross size round-trip regressed");
    Require(loaded.tools[1].mcfCrossThick == 4, "multi-color cross thickness round-trip regressed");
    Require(loaded.tools[1].mcfAnchorX == 21 && loaded.tools[1].mcfAnchorY == 22, "multi-color anchor round-trip regressed");
    Require(loaded.tools[1].mcfImgGray && loaded.tools[1].mcfImgBinary, "multi-color preprocess flags round-trip regressed");
    Require(loaded.tools[1].mcfImgBinThresh == 173, "multi-color binary threshold round-trip regressed");
    Require(loaded.tools[1].mcfRoiX == 2 && loaded.tools[1].mcfRoiH == 41, "multi-color ROI rect round-trip regressed");
    Require(loaded.tools[1].mcfRefImageBase64 == "iVBORw0KGgr/", "multi-color reference image base64 conversion regressed");
    Require(loaded.tools[1].mcfPointsJson == mcf.mcfPointsJson, "multi-color points round-trip regressed");
    Require(loaded.tools[2].type == 14, "QR tool type round-trip regressed");
    Require(!loaded.tools[2].qrUseROI && !loaded.tools[2].qrDetectMulti && !loaded.tools[2].qrEnhance,
        "QR boolean parameters round-trip regressed");
    Require(loaded.tools[2].qrMinSize == 37 && loaded.tools[2].qrEngine == 2,
        "QR numeric parameters round-trip regressed");
    Require(!loaded.tools[2].qrShowText, "QR label flag round-trip regressed");
    Require(loaded.tools[2].qrFormatMask == (BarcodeFormatCode128 | BarcodeFormatDataMatrix),
        "barcode format filter round-trip regressed");
    Require(!loaded.tools[2].qrFilterDuplicates, "barcode duplicate filter round-trip regressed");
    Require(loaded.tools[3].type == 15 && loaded.tools[3].measureMode == 3,
        "measurement tool recipe type regressed");
    Require(std::abs(loaded.tools[3].measureMmPerPixel - 0.025f) < 0.0001f &&
        loaded.tools[3].measureToleranceEnabled &&
        std::abs(loaded.tools[3].measureTolerancePlus - 0.2f) < 0.0001f,
        "measurement calibration/tolerance round-trip regressed");
    Require(loaded.tools[3].measureCaliperCount == 24 &&
        loaded.tools[3].measureEdgePolarity == 2 &&
        std::abs(loaded.tools[3].measureMinimumConfidence - 0.75f) < 0.0001f,
        "measurement caliper parameters round-trip regressed");
    Require(loaded.tools[3].measureCalibration.enabled &&
        loaded.tools[3].measureCalibration.homographyEnabled &&
        loaded.tools[3].measureCalibration.distortionEnabled &&
        std::abs(loaded.tools[3].measureCalibration.scaleY - 0.03) < 1.0e-9 &&
        std::abs(loaded.tools[3].measureCalibration.pixelToWorldHomography(0, 2) - 4.5) < 1.0e-9,
        "full calibration round-trip regressed");
    Require(loaded.tools[3].fixture.enabled && loaded.tools[3].fixture.sourceToolIndex == 0 &&
        cv::norm(loaded.tools[3].fixture.referenceOrigin - cv::Point2f(12.0f, 34.0f)) < 0.001f,
        "fixture settings round-trip regressed");

    std::filesystem::remove(path);
}

void TestSampleImageCorePipeline()
{
    const std::filesystem::path root = FindRepoRoot();
    const std::filesystem::path imagePath = root / "assets" / "images" / "test.jpg";
    cv::Mat image = cv::imread(imagePath.string(), cv::IMREAD_COLOR);
    Require(!image.empty(), "sample image load failed");

    ROI roi;
    roi.start = ImVec2(140.0f, 60.0f);
    roi.end = ImVec2((float)image.cols - 120.0f, (float)image.rows - 80.0f);

    VisionContext ctx;
    ctx.image = image;
    ctx.originalImage = image.clone();
    ctx.width = image.cols;
    ctx.height = image.rows;
    ctx.rois.push_back(roi);
    ctx.selectedROI = 0;

    ContourTool tool;
    tool.useGray = true;
    tool.blurSize = 1;
    tool.threshMode = 0;
    tool.minArea = 200.0f;
    tool.maxContours = 100;

    ITool* entry = &tool;
    ToolResult result = entry->Execute(ctx);
    Require(result.success, "sample contour tool execution failed");
    Require(!result.regions.empty(), "sample contour tool produced no regions");

    ctx.ClearUnifiedResults();
    ctx.unifiedResults.push_back(result);
    Require(ctx.unifiedResults.size() == 1, "ToolResult publish baseline regressed");

    cv::Mat overlay = DrawToolResultOverlay(image, result);
    Require(!overlay.empty(), "result overlay rendering failed");

    const std::filesystem::path outPath =
        std::filesystem::temp_directory_path() / "imgui_opencv_baseline_overlay.png";
    Require(cv::imwrite(outPath.string(), overlay), "result overlay save failed");
    std::filesystem::remove(outPath);
}

void TestLineToolSampleImage()
{
    const std::filesystem::path root = FindRepoRoot();
    const std::filesystem::path imagePath = root / "assets" / "images" / "test.jpg";
    cv::Mat image = cv::imread(imagePath.string(), cv::IMREAD_COLOR);
    Require(!image.empty(), "line sample image load failed");

    ROI roi;
    roi.start = ImVec2(140.0f, 60.0f);
    roi.end = ImVec2((float)image.cols - 120.0f, (float)image.rows - 80.0f);

    VisionContext ctx;
    ctx.image = image;
    ctx.rois.push_back(roi);
    ctx.selectedROI = 0;

    LineTool tool;
    tool.useROI = true;
    tool.cannyLow = 50;
    tool.cannyHigh = 150;
    tool.minLength = 80.0f;
    tool.maxGap = 12.0f;
    tool.maxLines = 20;

    ITool* entry = &tool;
    ToolResult result = entry->Execute(ctx);
    Require(result.success, "line tool execution failed");
    Require(!result.lines.empty(), "line tool produced no lines");
    Require(result.lines[0].length > 0.0f, "line tool produced invalid line length");

    cv::Mat overlay = DrawToolResultOverlay(image, result);
    Require(!overlay.empty(), "line overlay rendering failed");
}

void TestMultiColorFinderNoPoints()
{
    VisionContext ctx;
    ctx.image = cv::Mat::zeros(32, 32, CV_8UC3);

    MultiColorFinder tool;
    ITool* entry = &tool;
    ToolResult result = entry->Execute(ctx);

    Require(!result.success, "multi-color finder should fail when no points are configured");
    Require(result.message == "请至少添加1个颜色点", "multi-color finder failure message regressed");
}

void TestOCRToolMissingEngineFailsWithTextResultContract()
{
    VisionContext ctx;
    ctx.image = cv::Mat::zeros(64, 96, CV_8UC3);

    ROI roi;
    roi.start = ImVec2(8.0f, 10.0f);
    roi.end = ImVec2(58.0f, 42.0f);
    ctx.rois.push_back(roi);
    ctx.selectedROI = 0;

    OCRTool tool;
    tool.useROI = true;
    tool.detParamPath = "missing_det.ncnn.param";
    tool.detModelPath = "missing_det.ncnn.param";
    tool.recParamPath = "missing_rec.ncnn.param";
    tool.recModelPath = "missing_rec.ncnn.param";
    tool.dictionaryPath = "missing_keys.txt";
    tool.minConfidence = 0.35f;
    tool.maxItems = 250;
    tool.inputSize = 960;
    tool.maxCandidates = 320;
    tool.minBoxArea = 24;
    tool.minBoxHeight = 8;
    tool.roiPadding = 32;
    tool.fastMode = false;
    tool.detectOnly = true;

    ITool* entry = &tool;
    ToolResult result = entry->Execute(ctx);

    Require(!result.success, "OCR tool should fail when NCNN OCR engine is unavailable");
    Require(result.texts.empty(), "OCR tool without engine should not emit text items");
    Require(result.message.find("NCNN") != std::string::npos || result.message.find("model") != std::string::npos,
        "OCR tool failure message should mention missing NCNN engine or model file");
    Require(result.measurements.size() >= 4, "OCR tool should report the selected ROI even on engine failure");
    Require(result.measurements.size() >= 8, "OCR tool should report the expanded OCR input rect");

    nlohmann::json saved = tool.Save();
    OCRTool loaded;
    loaded.Load(saved);
    Require(loaded.detParamPath == tool.detParamPath, "OCR det param path save/load regressed");
    Require(loaded.detModelPath == tool.detModelPath, "OCR det model path save/load regressed");
    Require(loaded.recParamPath == tool.recParamPath, "OCR rec param path save/load regressed");
    Require(loaded.recModelPath == tool.recModelPath, "OCR rec model path save/load regressed");
    Require(loaded.dictionaryPath == tool.dictionaryPath, "OCR dictionary path save/load regressed");
    Require(std::abs(loaded.minConfidence - tool.minConfidence) < 0.0001f, "OCR confidence save/load regressed");
    Require(loaded.maxItems == tool.maxItems, "OCR max items save/load regressed");
    Require(loaded.inputSize == tool.inputSize, "OCR input size save/load regressed");
    Require(loaded.maxCandidates == tool.maxCandidates, "OCR max candidates save/load regressed");
    Require(loaded.minBoxArea == tool.minBoxArea, "OCR min box area save/load regressed");
    Require(loaded.minBoxHeight == tool.minBoxHeight, "OCR min box height save/load regressed");
    Require(loaded.roiPadding == tool.roiPadding, "OCR ROI padding save/load regressed");
    Require(loaded.fastMode == tool.fastMode, "OCR fast mode save/load regressed");
    Require(loaded.detectOnly == tool.detectOnly, "OCR detect-only save/load regressed");
    Require(loaded.useROI == tool.useROI, "OCR ROI flag save/load regressed");
}

void TestWindowsPPOCREngineUnavailableContract()
{
    WindowsPPOCRConfig cfg;
    cfg.detParamPath = "missing_det.ncnn.param";
    cfg.detModelPath = "missing_det.ncnn.bin";
    cfg.recParamPath = "missing_rec.ncnn.param";
    cfg.recModelPath = "missing_rec.ncnn.bin";
    cfg.dictionaryPath = "missing_keys.txt";

    WindowsPPOCREngine engine;
    std::string error;
    Require(!engine.Load(cfg, &error), "NCNN OCR engine should not load without NCNN support or model files");
    Require(error.find("NCNN") != std::string::npos || error.find("model") != std::string::npos,
        "NCNN OCR engine load failure should explain the missing dependency or model");

    std::vector<PPOCRTextResult> texts;
    error.clear();
    Require(!engine.Recognize(cv::Mat::zeros(16, 16, CV_8UC3), texts, &error),
        "NCNN OCR engine should not recognize before successful load");
    Require(texts.empty(), "NCNN OCR unavailable path should not emit text results");
    Require(!error.empty(), "NCNN OCR recognize failure should provide an error");
}

void TestWindowsPPOCRRecognitionCropKeepsHorizontalAspect()
{
    const cv::Size size = WindowsPPOCREngine::RecognitionCropSizeForTest(48.0f, 360.0f, 0);
    Require(size.height == 48, "OCR recognition crop height should match recognizer input height");
    Require(size.width >= 300, "OCR recognition crop collapsed horizontal text width");
    Require(size.width > size.height, "OCR recognition crop should preserve horizontal text aspect");
}

void TestWindowsPPOCREngineLoadsBundledModels()
{
    const std::filesystem::path root = FindRepoRoot();
    const std::filesystem::path modelDir = root / "models" / "ppocrv6";

    WindowsPPOCRConfig cfg;
    cfg.detParamPath = (modelDir / "PP_OCRv6_tiny_det.ncnn.param").string();
    cfg.detModelPath = (modelDir / "PP_OCRv6_tiny_det.ncnn.bin").string();
    cfg.recParamPath = (modelDir / "PP_OCRv6_tiny_rec.ncnn.param").string();
    cfg.recModelPath = (modelDir / "PP_OCRv6_tiny_rec.ncnn.bin").string();
    cfg.dictionaryPath = (modelDir / "ppocr_keys_v6_tiny.txt").string();
    cfg.inputSize = 320;
    cfg.minConfidence = 0.30f;
    cfg.maxItems = 1;

    WindowsPPOCREngine engine;
    std::string error;
    Require(engine.Load(cfg, &error), error.empty() ? "NCNN OCR bundled model load failed" : error.c_str());
    Require(engine.IsReady(), "NCNN OCR engine should be ready after loading bundled models");

    std::vector<PPOCRTextResult> texts;
    error.clear();
    Require(engine.Recognize(cv::Mat::zeros(64, 128, CV_8UC3), texts, &error),
        error.empty() ? "NCNN OCR blank image inference failed" : error.c_str());
    Require(texts.empty(), "NCNN OCR blank image should not emit text");
}

void TestWindowsPPOCREngineResolvesRelativeModelsFromReleaseDir()
{
    const std::filesystem::path root = FindRepoRoot();
    const std::filesystem::path originalCwd = std::filesystem::current_path();
    const std::filesystem::path tempCwd = std::filesystem::temp_directory_path() / "imgui_opencv_ocr_cwd";
    std::filesystem::create_directories(tempCwd);

    try {
        std::filesystem::current_path(tempCwd);
        const std::string resolved = WindowsPPOCREngine::ResolvePathForTest(
            "models\\ppocrv6\\PP_OCRv6_tiny_det.ncnn.param");
        std::filesystem::current_path(originalCwd);
        Require(std::filesystem::exists(resolved), "NCNN OCR relative model path did not resolve to an existing file");
        Require(resolved.find("ppocrv6") != std::string::npos, "NCNN OCR relative model path resolved to unexpected location");
    }
    catch (...) {
        std::filesystem::current_path(originalCwd);
        throw;
    }
}

void TestOCRToolDefaultRelativeModelsWorkOutsideReleaseCwd()
{
    const std::filesystem::path originalCwd = std::filesystem::current_path();
    const std::filesystem::path tempCwd = std::filesystem::temp_directory_path() / "imgui_opencv_ocr_tool_cwd";
    std::filesystem::create_directories(tempCwd);

    try {
        std::filesystem::current_path(tempCwd);

        VisionContext ctx;
        ctx.image = cv::Mat::zeros(64, 128, CV_8UC3);

        OCRTool tool;
        tool.useROI = false;
        tool.inputSize = 320;
        tool.maxItems = 1;

        ToolResult result = tool.Execute(ctx);
        ToolResult cached = tool.Execute(ctx);
        std::filesystem::current_path(originalCwd);

        Require(result.success, result.message.empty() ? "OCR tool relative model execution failed" : result.message.c_str());
        Require(result.message.find("missing") == std::string::npos, "OCR tool still reports missing model for default relative paths");
        Require(cached.success, "OCR tool cached execution should succeed");
        Require(cached.message.find("缓存") != std::string::npos, "OCR tool should reuse cached result for unchanged image and parameters");
    }
    catch (...) {
        std::filesystem::current_path(originalCwd);
        throw;
    }
}

void TestMorphologyToolITool()
{
    VisionContext ctx;
    ctx.image = cv::Mat::zeros(40, 40, CV_8UC3);
    cv::rectangle(ctx.image, cv::Rect(12, 12, 12, 12), cv::Scalar(255, 255, 255), cv::FILLED);

    ROI roi;
    roi.start = ImVec2(8.0f, 8.0f);
    roi.end = ImVec2(30.0f, 30.0f);
    ctx.rois.push_back(roi);
    ctx.selectedROI = 0;

    MorphologyITool tool;
    tool.params.opType = 1;
    tool.params.kernelSize = 1;
    tool.params.iterations = 1;

    ITool* entry = &tool;
    ToolResult result = entry->Execute(ctx);
    Require(result.success, "morphology ITool execution failed");
    Require(!result.debugImage.empty(), "morphology ITool produced no debug image");
    Require(result.debugImage.size() == ctx.image.size(), "morphology ITool output size regressed");
}

void TestColorAnalyzerITool()
{
    VisionContext ctx;
    ctx.image = cv::Mat(24, 24, CV_8UC3, cv::Scalar(10, 20, 30));

    ColorAnalyzerITool tool;
    tool.params.histBins = 16;
    tool.params.histHeight = 80;

    ITool* entry = &tool;
    ToolResult result = entry->Execute(ctx);
    Require(result.success, "color analyzer ITool execution failed");
    Require(result.measurements.size() >= 6, "color analyzer measurements regressed");
    Require(!result.debugImage.empty(), "color analyzer histogram image regressed");
    Require(std::abs(result.measurements[0].value - 10.0) < 0.001,
        "color analyzer mean channel regressed");
}

void TestBlobToolITool()
{
    VisionContext ctx;
    ctx.image = cv::Mat::zeros(64, 64, CV_8UC1);
    cv::rectangle(ctx.image, cv::Rect(10, 10, 12, 12), cv::Scalar(255), cv::FILLED);
    cv::rectangle(ctx.image, cv::Rect(36, 30, 10, 8), cv::Scalar(255), cv::FILLED);

    BlobTool tool;
    tool.minArea = 40;
    tool.maxArea = 300;

    ITool* entry = &tool;
    ToolResult result = entry->Execute(ctx);
    Require(result.success, "blob ITool execution failed");
    Require(result.regions.size() == 2, "blob ITool region count regressed");
    Require(result.regions[0].area >= 40.0f, "blob ITool area regressed");
}

void TestThresholdToolITool()
{
    VisionContext ctx;
    ctx.image = cv::Mat::zeros(32, 32, CV_8UC3);
    cv::rectangle(ctx.image, cv::Rect(8, 8, 12, 12), cv::Scalar(220, 220, 220), cv::FILLED);

    ROI roi;
    roi.start = ImVec2(6.0f, 6.0f);
    roi.end = ImVec2(24.0f, 24.0f);
    ctx.rois.push_back(roi);
    ctx.selectedROI = 0;

    ThresholdITool tool;
    tool.useGray = true;
    tool.enableThreshold = true;
    tool.threshold = 128;

    ITool* entry = &tool;
    ToolResult result = entry->Execute(ctx);
    Require(result.success, "threshold ITool execution failed");
    Require(!result.debugImage.empty(), "threshold ITool produced no debug image");
    Require(result.debugImage.size() == ctx.image.size(), "threshold ITool output size regressed");
}

void TestEdgeToolITool()
{
    VisionContext ctx;
    ctx.image = cv::Mat::zeros(48, 48, CV_8UC3);
    cv::rectangle(ctx.image, cv::Rect(12, 12, 20, 20), cv::Scalar(255, 255, 255), cv::FILLED);

    ROI roi;
    roi.start = ImVec2(8.0f, 8.0f);
    roi.end = ImVec2(38.0f, 38.0f);
    ctx.rois.push_back(roi);
    ctx.selectedROI = 0;

    EdgeTool tool;
    tool.useGray = true;
    tool.cannyLow = 30;
    tool.cannyHigh = 120;

    ITool* entry = &tool;
    ToolResult result = entry->Execute(ctx);
    Require(result.success, "edge ITool execution failed");
    Require(!result.debugImage.empty(), "edge ITool produced no debug image");
    Require(result.debugImage.size() == ctx.image.size(), "edge ITool output size regressed");
    Require(cv::countNonZero(result.debugImage.reshape(1)) > 0, "edge ITool produced blank output");
}

void TestFrameSourceStateUpdatesCurrentFrame()
{
    gImage.release();
    gOriginalImage.release();
    gContext.Clear();
    gImageWidth = 0;
    gImageHeight = 0;
    g_ImageVersion = 0;

    cv::Mat frame(18, 24, CV_8UC3, cv::Scalar(12, 34, 56));
    FrameSourceState::SetCurrentFrame(frame, FrameSourceType::ImageSequence, "seq/a.png", 3, 120.0);

    Require(!gImage.empty(), "frame source did not update gImage");
    Require(!gOriginalImage.empty(), "frame source did not update gOriginalImage");
    Require(!gContext.image.empty(), "frame source did not update VisionContext image");
    Require(!gContext.originalImage.empty(), "frame source did not update VisionContext original image");
    Require(gImage.cols == 24 && gImage.rows == 18, "frame source dimensions regressed");
    Require(gImageWidth == 24 && gImageHeight == 18, "frame source global size regressed");
    Require(gContext.width == 24 && gContext.height == 18, "frame source context size regressed");
    Require(g_ImageVersion == 1 && gContext.imageVersion == 1, "frame source version regressed");

    const FramePacket& packet = FrameSourceState::Current();
    Require(packet.sourceType == FrameSourceType::ImageSequence, "frame source type regressed");
    Require(packet.sourcePath == "seq/a.png", "frame source path regressed");
    Require(packet.frameIndex == 3, "frame source index regressed");
    Require(std::abs(packet.timestampMs - 120.0) < 0.001, "frame source timestamp regressed");

    frame.setTo(cv::Scalar(200, 200, 200));
    Require(gOriginalImage.at<cv::Vec3b>(0, 0)[0] == 12, "frame source kept shallow original copy");

    FrameSourceState::Clear();
    Require(!FrameSourceState::HasFrame(), "frame source clear left stale current frame");
    Require(!gContext.frame.valid(), "frame source clear left stale context frame");
}

void TestImageStateOwnsCurrentImageSnapshot()
{
    gImage.release();
    gOriginalImage.release();
    gContext.Clear();
    gImageWidth = 0;
    gImageHeight = 0;
    g_ImageVersion = 0;
    ImageState::Clear();

    cv::Mat image(10, 14, CV_8UC3, cv::Scalar(7, 8, 9));
    ImageState::SetImage(image);

    Require(ImageState::HasImage(), "image state did not accept image");
    Require(ImageState::Width() == 14 && ImageState::Height() == 10, "image state dimensions regressed");
    Require(ImageState::Version() == 1, "image state version did not increment");
    Require(!ImageState::Current().empty(), "image state current image empty");
    Require(!ImageState::Original().empty(), "image state original image empty");
    Require(gImageWidth == 14 && gImageHeight == 10, "image state did not sync legacy dimensions");
    Require(g_ImageVersion == 1 && gContext.imageVersion == 1, "image state did not sync legacy version");
    gImageWidth = 99;
    gImageHeight = 88;
    g_ImageVersion = 77;
    Require(ImageState::Width() == 99 && ImageState::Height() == 88,
        "legacy image dimensions should reference ImageState dimensions");
    Require(ImageState::Version() == 77, "legacy image version should reference ImageState version");
    Require(gImage.data == ImageState::Current().data, "legacy gImage should reference ImageState current image");
    Require(gOriginalImage.data == ImageState::Original().data, "legacy gOriginalImage should reference ImageState original image");
    Require(gContext.image.data != ImageState::Current().data, "image state shared VisionContext current image buffer");

    gImage.setTo(cv::Scalar(11, 12, 13));
    Require(ImageState::Current().at<cv::Vec3b>(0, 0)[0] == 11, "legacy gImage write did not update ImageState current image");

    image.setTo(cv::Scalar(200, 200, 200));
    Require(ImageState::Current().at<cv::Vec3b>(0, 0)[0] == 11, "image state current buffer was unexpectedly replaced");
    Require(gOriginalImage.at<cv::Vec3b>(0, 0)[0] == 7, "image state kept shallow legacy original copy");

    ImageState::Clear();
    Require(!ImageState::HasImage(), "image state clear left current image");
    Require(gImage.empty() && gOriginalImage.empty(), "image state clear did not clear legacy images");
    Require(gImageWidth == 0 && gImageHeight == 0, "image state clear did not reset dimensions");
    Require(gContext.image.empty() && gContext.originalImage.empty(), "image state clear did not clear context images");
}

void TestRecipeCaptureUsesCurrentFramePath()
{
    gContext.Clear();
    FrameSourceState::Clear();

    cv::Mat frame(12, 16, CV_8UC3, cv::Scalar(1, 2, 3));
    FrameSourceState::SetCurrentFrame(frame, FrameSourceType::SingleImage, "C:/sample/input.png");

    RecipeData data = RecipeManager::Capture("frame_path");
    Require(data.imagePath == "C:/sample/input.png", "recipe capture did not preserve current image path");

    FrameSourceState::Clear();
}

void TestToolExecutorInjectsImageSnapshot()
{
    gImage = cv::Mat::zeros(32, 32, CV_8UC1);
    cv::rectangle(gImage, cv::Rect(8, 8, 8, 8), cv::Scalar(255), cv::FILLED);
    gOriginalImage = gImage.clone();
    gContext.Clear();

    ToolInstance it;
    it.type = 2;
    it.blobMinArea = 20;
    it.blobMaxArea = 200;

    ToolExecutor::Execute(it.type, it);
    Require(gContext.image.data == gImage.data, "tool executor did not share read-only blob input");
    Require(!gContext.unifiedResults.empty(), "tool executor did not publish result");
    Require(gContext.unifiedResults[0].regions.size() == 1, "tool executor blob result regressed");

    ToolInstance threshold;
    threshold.type = 3;
    threshold.dbgUseGray = false;
    threshold.dbgEnableThresh = true;
    threshold.dbgThreshold = 100;
    ToolExecutor::Execute(threshold.type, threshold);
    Require(gContext.image.data != gImage.data, "tool executor shared mutable threshold input");
}

void TestToolChainEditActions()
{
    std::vector<ToolInstance> tools(3);
    tools[0].type = 12;
    tools[1].type = 5;
    tools[2].type = 7;

    int activeIndex = 2;
    int liveIndex = 2;
    bool liveDetect = true;

    Require(UI::MoveToolInstance(tools, 2, 1, activeIndex, liveIndex, liveDetect),
        "tool chain move up was rejected");
    Require(tools[1].type == 7 && tools[2].type == 5, "tool chain move order regressed");
    Require(activeIndex == 1 && liveIndex == 1 && liveDetect,
        "tool chain move did not preserve active/live index");

    Require(!UI::MoveToolInstance(tools, 1, 0, activeIndex, liveIndex, liveDetect),
        "tool chain allowed moving ahead of original tool");
    Require(!UI::RemoveToolInstance(tools, 0, activeIndex, liveIndex, liveDetect),
        "tool chain allowed deleting original tool");

    bool destroyed = false;
    tools[1].toolImpl = new TestDisposableTool(&destroyed);
    Require(UI::RemoveToolInstance(tools, 1, activeIndex, liveIndex, liveDetect),
        "tool chain remove was rejected");
    Require(destroyed, "tool chain remove did not release tool implementation");
    Require(tools.size() == 2 && tools[0].type == 12 && tools[1].type == 5,
        "tool chain remove order regressed");
    Require(activeIndex == -1 && liveIndex == -1 && !liveDetect,
        "tool chain remove did not clear selected live tool");
}

void TestShapeMaxResultsDefaultIsOne()
{
    ToolInstance instance;
    RecipeToolInstance recipeTool;
    ShapeTool shapeTool;

    Require(instance.shpMaxResults == 1, "shape tool UI default max results should be 1");
    Require(recipeTool.shpMaxResults == 1, "shape recipe default max results should be 1");
    Require(shapeTool.maxResults == 1, "shape ITool default max results should be 1");
}

void TestToolInstanceLabelDefaultIsEmpty()
{
    ToolInstance instance;
    Require(instance.label.empty(), "tool instance label should default to empty");
}

void TestCoreStateOwnsRoiAndToolChain()
{
    UI::gROIs.clear();
    UI::gSelectedROI = -1;
    UI::g_ToolInstances.clear();
    UI::g_ActiveToolIndex = -1;
    UI::g_YoloLiveDetect = false;
    UI::g_YoloLiveInstanceIdx = -1;
    UI::g_YoloLastTimeMs = 0.0f;
    UI::g_YoloLiveFrameMs = 0.0f;

    ROIState::Items().clear();
    ROI roi;
    roi.start = ImVec2(1.0f, 2.0f);
    roi.end = ImVec2(3.0f, 4.0f);
    ROIState::Items().push_back(roi);
    ROIState::SetSelectedIndex(0);

    Require(ROIState::ReadOnlyItems().size() == 1, "core ROI state did not store ROI");
    Require(ROIState::SelectedIndex() == 0, "core ROI selected index regressed");
    Require(UI::gROIs.size() == 1, "UI ROI compatibility view did not use core state");
    Require(UI::gSelectedROI == 0, "UI ROI selected compatibility view did not use core state");

    ToolChainState::Tools().clear();
    ToolInstance tool;
    tool.type = 7;
    tool.label = "core";
    ToolChainState::Tools().push_back(tool);
    ToolChainState::SetActiveIndex(0);
    ToolChainState::SetYoloLiveDetect(true);
    ToolChainState::SetYoloLiveInstanceIndex(0);
    ToolChainState::SetYoloLastTimeMs(12.5f);
    ToolChainState::SetYoloLiveFrameMs(16.0f);

    Require(ToolChainState::ReadOnlyTools().size() == 1, "core tool chain did not store tool");
    Require(ToolChainState::ActiveIndex() == 0, "core tool active index regressed");
    Require(ToolChainState::YoloLiveDetect(), "core YOLO live flag regressed");
    Require(ToolChainState::YoloLiveInstanceIndex() == 0, "core YOLO live index regressed");
    Require(std::abs(ToolChainState::YoloLastTimeMs() - 12.5f) < 0.001f, "core YOLO last time regressed");
    Require(std::abs(ToolChainState::YoloLiveFrameMs() - 16.0f) < 0.001f, "core YOLO frame time regressed");

    Require(UI::g_ToolInstances.size() == 1, "UI tool compatibility view did not use core state");
    Require(UI::g_ActiveToolIndex == 0, "UI active tool compatibility view did not use core state");
    Require(UI::g_YoloLiveDetect, "UI YOLO live compatibility view did not use core state");
    Require(UI::g_YoloLiveInstanceIdx == 0, "UI YOLO live index compatibility view did not use core state");
}

void TestShapeMatcherTemplateLargerThanSearchImage()
{
    cv::Mat image(20, 60, CV_8UC3, cv::Scalar(0, 0, 0));
    cv::rectangle(image, cv::Rect(4, 4, 10, 10), cv::Scalar(255, 255, 255), cv::FILLED);

    cv::Mat tpl(40, 40, CV_8UC3, cv::Scalar(0, 0, 0));
    cv::rectangle(tpl, cv::Rect(8, 8, 20, 20), cv::Scalar(255, 255, 255), cv::FILLED);

    ShapeMatcher::Params params;
    params.maxResults = 1;

    const auto matches = ShapeMatcher::Search(image, tpl, params, {});
    Require(matches.empty(), "shape matcher should return no matches when template does not fit inside search image");
}

std::string ReadTextFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void TestResultExporterWritesResultsAndReport()
{
    ResultExporter::ExportSnapshot snapshot;
    snapshot.recipeName = "export_test";
    snapshot.imagePath = "sample.jpg";
    snapshot.imageWidth = 64;
    snapshot.imageHeight = 48;
    snapshot.resultImagePath = "result.png";
    snapshot.totalTimeMs = 1.5f;

    ToolInstance tool;
    tool.type = 10;
    tool.label = "找到色";
    snapshot.tools.push_back(tool);
    snapshot.toolTimesMs.push_back(0.25f);

    ToolResult result;
    result.toolName = "多点找色[找到色]";
    ToolResult::Region region;
    region.bbox = cv::Rect(1, 2, 3, 4);
    region.score = 0.99f;
    region.label = "找到色 #1";
    result.regions.push_back(region);
    snapshot.results.push_back(result);

    const auto dir = std::filesystem::temp_directory_path() / "imgui_opencv_regression";
    std::filesystem::create_directories(dir);
    const auto jsonPath = dir / "results_export.json";
    const auto reportPath = dir / "run_report.md";

    Require(ResultExporter::ExportResultsJson(jsonPath.string().c_str(), snapshot), "result json export failed");
    Require(ResultExporter::ExportRunReportMarkdown(reportPath.string().c_str(), snapshot), "run report export failed");

    const std::string jsonText = ReadTextFile(jsonPath);
    const std::string reportText = ReadTextFile(reportPath);
    Require(jsonText.find("\"kind\": \"vision_results\"") != std::string::npos, "result json missing kind");
    Require(jsonText.find("\"resultImagePath\": \"result.png\"") != std::string::npos, "result json missing result image path");
    Require(jsonText.find("找到色 #1") != std::string::npos, "result json missing region label");
    Require(reportText.find("运行报告") != std::string::npos, "run report missing title");
    Require(reportText.find("结果图像: result.png") != std::string::npos, "run report missing result image path");
    Require(reportText.find("多点找色") != std::string::npos, "run report missing tool name");
}

void TestToolExecutorResolvesMovedRuntimeRoi()
{
    gImage = cv::Mat::zeros(80, 120, CV_8UC1);
    gOriginalImage = gImage.clone();
    gContext.Clear();
    ROIState::Items().clear();

    ROI configured;
    configured.runtimeId = 42;
    configured.type = ROI_TYPE_LINE;
    configured.start = {5.0f, 10.0f};
    configured.end = {25.0f, 10.0f};

    ROI moved = configured;
    moved.start = {60.0f, 50.0f};
    moved.end = {90.0f, 50.0f};
    ROIState::Items().push_back(moved);

    ToolInstance measurement;
    measurement.type = 15;
    measurement.measureMode = 0;
    measurement.searchROIs.push_back(configured);
    ToolExecutor::Execute(measurement.type, measurement);

    Require(measurement.hasLastResult && measurement.lastResult.success,
        "runtime-linked measurement execution failed");
    Require(!measurement.lastResult.lines.empty() &&
        measurement.lastResult.lines.front().p1 == cv::Point(60, 50) &&
        measurement.lastResult.lines.front().p2 == cv::Point(90, 50),
        "tool executor used stale ROI coordinates after visible ROI movement");
}

void TestResultOverlayStatePolicy()
{
    auto& settings = ResultOverlayState::MutableSettings();
    const auto oldSettings = settings;
    auto& tools = ToolChainState::Tools();
    const auto oldTools = tools;

    settings.showLabels = true;
    settings.avoidLabelOverlap = true;
    settings.maxVisibleLabels = 30;
    tools.clear();
    ToolInstance tool;
    tool.type = 10;
    tool.showResultLabels = false;
    tools.push_back(tool);

    ToolResult result;
    result.sourceToolIndex = 0;
    Require(!ResultOverlayState::ShouldDrawResultLabels(result), "per-tool result label switch regressed");

    tools[0].showResultLabels = true;
    Require(ResultOverlayState::ShouldDrawResultLabels(result), "enabled result labels should draw");

    settings.showLabels = false;
    Require(!ResultOverlayState::ShouldDrawResultLabels(result), "global result label switch regressed");

    settings.showLabels = true;
    settings.maxVisibleLabels = 0;
    Require(ResultOverlayState::MaxVisibleLabels() == 0, "max visible label setting regressed");

    ToolResult textResult;
    ToolResult::TextItem text;
    text.text = "abc";
    textResult.texts.push_back(text);
    Require(!ResultOverlayState::ShouldDrawRegionLabel(textResult, "abc"), "duplicate text/region label filter regressed");

    settings = oldSettings;
    tools = oldTools;
}
}

int main(int argc, char** argv)
{
    try {
        if (argc > 1 && std::string(argv[1]) == "--qr-only") {
            TestQRCodeToolRecognizesBundledSample();
            std::cout << "regression_tests: QR checks passed\n";
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--policy-only") {
            TestRecursiveImageFolderScanSupportsCommonFormats();
            TestToolJudgementPolicy();
            TestIndustrialMeasurement();
            TestResultROIResolution();
            TestTemplateMatchingToolUsesInstanceParameters();
            TestToolChainReorderRemapsResultROISource();
            TestRecipeRoundTrip();
            TestToolExecutorResolvesMovedRuntimeRoi();
            std::cout << "regression_tests: import and judgement checks passed\n";
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--caliper-only") {
            TestCaliperOperators();
            TestCalibrationModel();
            TestFixtureTransform();
            std::cout << "regression_tests: caliper checks passed\n";
            return 0;
        }
        TestTemplateMatch();
        TestRoiConversion();
        TestYoloToolNoModelPath();
        TestQRCodeToolRecognizesBundledSample();
        TestRecursiveImageFolderScanSupportsCommonFormats();
        TestToolJudgementPolicy();
        TestIndustrialMeasurement();
        TestCaliperOperators();
        TestCalibrationModel();
        TestFixtureTransform();
        TestResultROIResolution();
        TestTemplateMatchingToolUsesInstanceParameters();
        TestToolChainReorderRemapsResultROISource();
        TestRecipeRoundTrip();
        TestSampleImageCorePipeline();
        TestLineToolSampleImage();
        TestMultiColorFinderNoPoints();
        TestOCRToolMissingEngineFailsWithTextResultContract();
        TestWindowsPPOCREngineUnavailableContract();
        TestWindowsPPOCRRecognitionCropKeepsHorizontalAspect();
        TestWindowsPPOCREngineLoadsBundledModels();
        TestWindowsPPOCREngineResolvesRelativeModelsFromReleaseDir();
        TestOCRToolDefaultRelativeModelsWorkOutsideReleaseCwd();
        TestMorphologyToolITool();
        TestColorAnalyzerITool();
        TestBlobToolITool();
        TestThresholdToolITool();
        TestEdgeToolITool();
        TestFrameSourceStateUpdatesCurrentFrame();
        TestImageStateOwnsCurrentImageSnapshot();
        TestRecipeCaptureUsesCurrentFramePath();
        TestToolExecutorInjectsImageSnapshot();
        TestToolExecutorResolvesMovedRuntimeRoi();
        TestToolChainEditActions();
        TestShapeMaxResultsDefaultIsOne();
        TestToolInstanceLabelDefaultIsEmpty();
        TestCoreStateOwnsRoiAndToolChain();
        TestShapeMatcherTemplateLargerThanSearchImage();
        TestResultOverlayStatePolicy();
        TestResultExporterWritesResultsAndReport();
        std::cout << "regression_tests: all tests passed\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "regression_tests: " << e.what() << "\n";
        return 1;
    }
}
