#include "../Algorithm/BlobTool.h"
#include "../Algorithm/ColorAnalyzer.h"
#include "../Algorithm/EdgeTool.h"
#include "../Algorithm/MorphologyTool.h"
#include "../Algorithm/ThresholdTool.h"
#include "../Algorithm/YOLOTool.h"
#include "../Algorithm/ShapeMatcher.h"
#include "../Algorithm/ShapeTools.h"
#include "../Algorithm/MultiColorFinder.h"
#include "../Core/RecipeManager.h"
#include "../Core/FrameSourceState.h"
#include "../Core/ImageState.h"
#include "../Core/ROIState.h"
#include "../Core/ToolChainState.h"
#include "../Core/ToolExecutor.h"
#include "../Core/VisionContext.h"
#include "../UI/ROIManager.h"
#include "../UI/ToolsWindow.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include <cmath>
#include <filesystem>
#include <iostream>
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

    std::filesystem::path path = std::filesystem::temp_directory_path() / "imgui_opencv_regression.recipe";
    std::filesystem::remove(path);

    Require(RecipeManager::Save(path.string().c_str(), data), "recipe save failed");

    RecipeData loaded;
    Require(RecipeManager::Load(path.string().c_str(), loaded), "recipe load failed");

    Require(loaded.name == data.name, "recipe name round-trip regressed");
    Require(loaded.threshold.useGray == data.threshold.useGray, "threshold round-trip regressed");
    Require(loaded.threshold.thresholdValue == data.threshold.thresholdValue, "threshold value round-trip regressed");
    Require(loaded.rois.size() == 1 && loaded.rois[0].endX == 30.0f, "ROI round-trip regressed");
    Require(loaded.tools.size() == 2, "tool count round-trip regressed");
    Require(loaded.tools[0].type == 4, "YOLO tool type round-trip regressed");
    Require(loaded.tools[0].label == "定位A", "tool label round-trip regressed");
    Require(loaded.tools[0].yoloUseROI, "YOLO ROI flag round-trip regressed");
    Require(std::abs(loaded.tools[0].yoloConfThreshold - 0.67f) < 0.001f,
        "YOLO confidence round-trip regressed");
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
    Require(gContext.image.data != gImage.data, "tool executor injected shared gImage buffer");
    Require(!gContext.unifiedResults.empty(), "tool executor did not publish result");
    Require(gContext.unifiedResults[0].regions.size() == 1, "tool executor blob result regressed");
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
}

int main()
{
    try {
        TestTemplateMatch();
        TestRoiConversion();
        TestYoloToolNoModelPath();
        TestRecipeRoundTrip();
        TestSampleImageCorePipeline();
        TestLineToolSampleImage();
        TestMultiColorFinderNoPoints();
        TestMorphologyToolITool();
        TestColorAnalyzerITool();
        TestBlobToolITool();
        TestThresholdToolITool();
        TestEdgeToolITool();
        TestFrameSourceStateUpdatesCurrentFrame();
        TestImageStateOwnsCurrentImageSnapshot();
        TestRecipeCaptureUsesCurrentFramePath();
        TestToolExecutorInjectsImageSnapshot();
        TestToolChainEditActions();
        TestShapeMaxResultsDefaultIsOne();
        TestToolInstanceLabelDefaultIsEmpty();
        TestCoreStateOwnsRoiAndToolChain();
        TestShapeMatcherTemplateLargerThanSearchImage();
        std::cout << "regression_tests: all tests passed\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "regression_tests: " << e.what() << "\n";
        return 1;
    }
}
