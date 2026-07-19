#include "../Algorithm/BlobTool.h"
#include "../Algorithm/ColorAnalyzer.h"
#include "../Algorithm/DifferenceTool.h"
#include "../Algorithm/EdgeTool.h"
#include "../Algorithm/GeometryDrawTool.h"
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
#include "../Core/CalibrationFitter.h"
#include "../Core/FrameSourceState.h"
#include "../Core/FrameNavigation.h"
#include "../Core/FixtureTransform.h"
#include "../Core/ImageState.h"
#include "../Core/ImageImportService.h"
#include "../Core/AsyncImageLoader.h"
#include "../Core/ImageViewState.h"
#include "../Core/HardwareAdapters.h"
#include "../Core/HardwareRuntimeService.h"
#include "../Core/ModbusTcpAdapter.h"
#include "../Core/TcpTextAdapter.h"
#include "../Core/ModbusPlcAdapter.h"
#include "../Core/OpenCvCameraAdapter.h"
#include "../Core/Open62541OpcUaAdapter.h"
#include "../Core/OpenFileDialog.h"
#include "../Core/ResultOverlayState.h"
#include "../Core/RealtimeDetectionState.h"
#include "../Core/ResultROIResolver.h"
#include "../Core/ResultExporter.h"
#include "../Core/InspectionHistory.h"
#include "../Core/ROIState.h"
#include "../Core/ROIEditorState.h"
#include "../Core/RotatedROI.h"
#include "../Core/ToolChainState.h"
#include "../Core/ToolChainValidator.h"
#include "../Core/ToolChainPreflight.h"
#include "../Core/ToolController.h"
#include "../Core/ToolAssetService.h"
#include "../Core/ToolROIService.h"
#include "../Core/ToolExecutor.h"
#include "../Core/ToolJudgement.h"
#include "../Core/VisionContext.h"
#include "../UI/ROIManager.h"
#include "../UI/ToolsWindow.h"
#include "../third_party/open62541/open62541.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
class LocalOpcUaTestServer
{
public:
    ~LocalOpcUaTestServer()
    {
        Stop();
    }

    bool Start()
    {
        for (std::uint16_t candidate = 48550; candidate < 48570; ++candidate)
        {
            server_ = UA_Server_new();
            if (!server_)
                return false;

            UA_StatusCode status = UA_ServerConfig_setMinimal(
                UA_Server_getConfig(server_), candidate, nullptr);
            if (status == UA_STATUSCODE_GOOD)
                status = AddNodes();
            if (status == UA_STATUSCODE_GOOD)
                status = UA_Server_run_startup(server_);
            if (status == UA_STATUSCODE_GOOD)
            {
                port_ = candidate;
                running_.store(true);
                worker_ = std::thread([this]()
                {
                    while (running_.load())
                    {
                        UA_Server_run_iterate(server_, false);
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                });
                return true;
            }

            UA_Server_delete(server_);
            server_ = nullptr;
        }
        return false;
    }

    void Stop()
    {
        running_.store(false);
        if (worker_.joinable())
            worker_.join();
        if (server_)
        {
            UA_Server_run_shutdown(server_);
            UA_Server_delete(server_);
            server_ = nullptr;
        }
        port_ = 0;
    }

    std::uint16_t Port() const
    {
        return port_;
    }

private:
    UA_StatusCode AddVariable(const char* nodeName, const void* initialValue,
        const UA_DataType& type)
    {
        UA_VariableAttributes attributes = UA_VariableAttributes_default;
        UA_StatusCode status = UA_Variant_setScalarCopy(
            &attributes.value, initialValue, &type);
        if (status != UA_STATUSCODE_GOOD)
            return status;
        attributes.dataType = type.typeId;
        attributes.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
        attributes.displayName = UA_LOCALIZEDTEXT_ALLOC("en-US", nodeName);

        status = UA_Server_addVariableNode(server_,
            UA_NODEID_STRING(1, const_cast<char*>(nodeName)),
            UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
            UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
            UA_QUALIFIEDNAME(1, const_cast<char*>(nodeName)),
            UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
            attributes, nullptr, nullptr);
        UA_VariableAttributes_clear(&attributes);
        return status;
    }

    UA_StatusCode AddNodes()
    {
        const UA_Boolean inspectionPass = UA_FALSE;
        UA_StatusCode status = AddVariable("InspectionPass", &inspectionPass,
            UA_TYPES[UA_TYPES_BOOLEAN]);
        const UA_Int32 count = 7;
        if (status == UA_STATUSCODE_GOOD)
            status = AddVariable("Count", &count, UA_TYPES[UA_TYPES_INT32]);
        const UA_Float ratio = 1.25f;
        if (status == UA_STATUSCODE_GOOD)
            status = AddVariable("Ratio", &ratio, UA_TYPES[UA_TYPES_FLOAT]);
        UA_String lineName = UA_STRING(const_cast<char*>("line-a"));
        if (status == UA_STATUSCODE_GOOD)
            status = AddVariable("LineName", &lineName, UA_TYPES[UA_TYPES_STRING]);
        return status;
    }

    UA_Server* server_ = nullptr;
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::uint16_t port_ = 0;
};

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

struct TestInputCaptureTool final : ITool
{
    TestInputCaptureTool(int* capturedValue, int outputValue = -1)
        : captured(capturedValue), output(outputValue)
    {
    }

    const char* GetName() const override { return "input-capture"; }
    int GetType() const override { return 2; }
    ToolResult Execute(VisionContext& context) override
    {
        ToolResult result;
        result.toolName = GetName();
        if (captured)
            *captured = context.image.empty() ? -1 : static_cast<int>(context.image.ptr<uchar>(0)[0]);
        if (output >= 0)
            result.debugImage = cv::Mat(context.image.size(), context.image.type(), cv::Scalar::all(output));
        return result;
    }
    void DrawUI() override {}
    nlohmann::json Save() const override { return {}; }
    void Load(const nlohmann::json&) override {}

    int* captured = nullptr;
    int output = -1;
};

struct TestCameraAdapter final : ICameraAdapter
{
    explicit TestCameraAdapter(bool* disconnectedFlag) : disconnected(disconnectedFlag) {}
    const char* AdapterName() const override { return "test-camera"; }
    DeviceOperationResult Connect(const DeviceEndpoint&) override
    {
        state = DeviceConnectionState::Connected;
        return {true, {}};
    }
    void Disconnect() override
    {
        state = DeviceConnectionState::Disconnected;
        if (disconnected) *disconnected = true;
    }
    DeviceConnectionState ConnectionState() const override { return state; }
    std::string LastError() const override { return {}; }
    DeviceOperationResult GrabFrame(cv::Mat& frame, int) override
    {
        frame = cv::Mat(4, 6, CV_8UC1, cv::Scalar(17)).clone();
        return {true, {}};
    }
    DeviceOperationResult StartStream() override { return {true, {}}; }
    void StopStream() override { stopped = true; }

    DeviceConnectionState state = DeviceConnectionState::Disconnected;
    bool stopped = false;
    bool* disconnected = nullptr;
};

struct ScriptedCameraBackend final : ICameraCaptureBackend
{
    DeviceOperationResult Open(const DeviceEndpoint& value) override
    {
        endpoint = value;
        open = openSucceeds;
        return open ? DeviceOperationResult{true, {}}
                    : DeviceOperationResult{false, "scripted camera open failure"};
    }

    void Close() override
    {
        open = false;
        ++closeCount;
        if (externalCloseCount)
            ++*externalCloseCount;
    }

    DeviceOperationResult Read(cv::Mat& frame, int timeoutMs) override
    {
        lastTimeoutMs = timeoutMs;
        if (!open)
            return {false, "scripted camera is closed"};
        if (!readSucceeds)
            return {false, "scripted camera read failure"};
        frame = nextFrame.clone();
        return {true, {}};
    }

    DeviceEndpoint endpoint;
    cv::Mat nextFrame = cv::Mat(5, 7, CV_8UC3, cv::Scalar(3, 5, 7));
    bool openSucceeds = true;
    bool readSucceeds = true;
    bool open = false;
    int closeCount = 0;
    int* externalCloseCount = nullptr;
    int lastTimeoutMs = 0;
};

struct TestPlcAdapter final : IPlcAdapter
{
    explicit TestPlcAdapter(bool* disconnectedFlag) : disconnected(disconnectedFlag) {}
    const char* AdapterName() const override { return "test-plc"; }
    DeviceOperationResult Connect(const DeviceEndpoint&) override
    {
        state = DeviceConnectionState::Connected;
        return {true, {}};
    }
    void Disconnect() override
    {
        state = DeviceConnectionState::Disconnected;
        if (disconnected) *disconnected = true;
    }
    DeviceConnectionState ConnectionState() const override { return state; }
    std::string LastError() const override { return {}; }
    DeviceOperationResult ReadTag(const std::string& tag, DeviceValue& value) override
    {
        if (tag != "ready") return {false, "unknown tag"};
        value = true;
        return {true, {}};
    }
    DeviceOperationResult WriteTag(const std::string& tag, const DeviceValue& value) override
    {
        lastWriteTag = tag;
        lastWriteValue = value;
        return {true, {}};
    }

    DeviceConnectionState state = DeviceConnectionState::Disconnected;
    bool* disconnected = nullptr;
    std::string lastWriteTag;
    DeviceValue lastWriteValue = false;
};

struct TestModbusAdapter final : IModbusTcpAdapter
{
    const char* AdapterName() const override { return "test-modbus"; }
    DeviceOperationResult Connect(const DeviceEndpoint&) override
    {
        state = DeviceConnectionState::Connected;
        return {true, {}};
    }
    void Disconnect() override { state = DeviceConnectionState::Disconnected; }
    DeviceConnectionState ConnectionState() const override { return state; }
    std::string LastError() const override { return {}; }
    DeviceOperationResult ReadCoils(std::uint16_t, std::uint16_t count,
        std::vector<bool>& values) override
    {
        values.assign(count, nextCoilValue);
        return {true, {}};
    }
    DeviceOperationResult WriteCoil(std::uint16_t address, bool value) override
    {
        lastAddress = address;
        lastValue = value;
        return {true, {}};
    }
    DeviceOperationResult ReadHoldingRegisters(std::uint16_t, std::uint16_t count,
        std::vector<std::uint16_t>& values) override
    {
        values.assign(count, nextRegisterValue);
        return {true, {}};
    }
    DeviceOperationResult WriteHoldingRegister(std::uint16_t address, std::uint16_t value) override
    {
        lastRegisterAddress = address;
        lastRegisterValue = value;
        return {true, {}};
    }

    DeviceConnectionState state = DeviceConnectionState::Disconnected;
    std::uint16_t lastAddress = 0;
    bool lastValue = false;
    bool nextCoilValue = false;
    std::uint16_t nextRegisterValue = 0;
    std::uint16_t lastRegisterAddress = 0;
    std::uint16_t lastRegisterValue = 0;
};

struct ScriptedModbusTransport final : IModbusTcpTransport
{
    DeviceOperationResult Connect(
        const std::string& address, std::uint16_t port, int timeoutMs) override
    {
        connected = connectSucceeds;
        lastAddress = address;
        lastPort = port;
        lastTimeoutMs = timeoutMs;
        return connected ? DeviceOperationResult{true, {}}
                         : DeviceOperationResult{false, "scripted connect failure"};
    }

    void Disconnect() override
    {
        connected = false;
    }

    DeviceOperationResult Exchange(const std::vector<std::uint8_t>& request,
        std::vector<std::uint8_t>& response) override
    {
        lastRequest = request;
        if (!connected)
            return {false, "scripted transport disconnected"};
        if (failNextExchange)
        {
            failNextExchange = false;
            return {false, "scripted exchange failure"};
        }
        if (request.size() < 8)
            return {false, "scripted request too short"};

        const std::uint8_t function = request[7];
        std::vector<std::uint8_t> pdu;
        if (exceptionNext)
        {
            exceptionNext = false;
            pdu = {static_cast<std::uint8_t>(function | 0x80), 2};
        }
        else if (function == 1)
        {
            pdu = {1, 1, 0x05};
        }
        else if (function == 3)
        {
            pdu = {3, 4, 0x12, 0x34, 0xab, 0xcd};
        }
        else if (function == 5 || function == 6)
        {
            pdu.push_back(function);
            pdu.insert(pdu.end(), request.begin() + 8, request.end());
        }
        else
        {
            return {false, "unexpected scripted function"};
        }

        response = {
            request[0], request[1], 0, 0,
            static_cast<std::uint8_t>((pdu.size() + 1) >> 8),
            static_cast<std::uint8_t>((pdu.size() + 1) & 0xff),
            request[6]
        };
        response.insert(response.end(), pdu.begin(), pdu.end());
        return {true, {}};
    }

    bool connectSucceeds = true;
    bool connected = false;
    bool exceptionNext = false;
    bool failNextExchange = false;
    std::string lastAddress;
    std::uint16_t lastPort = 0;
    int lastTimeoutMs = 0;
    std::vector<std::uint8_t> lastRequest;
};

struct ScriptedTcpTextTransport final : ITcpTextTransport
{
    DeviceOperationResult Connect(
        const std::string& address, std::uint16_t port, int timeoutMs) override
    {
        connected = connectSucceeds;
        lastAddress = address;
        lastPort = port;
        lastTimeoutMs = timeoutMs;
        return connected ? DeviceOperationResult{true, {}}
                         : DeviceOperationResult{false, "scripted TCP connect failure"};
    }

    void Disconnect() override
    {
        connected = false;
    }

    DeviceOperationResult Send(const std::string& text) override
    {
        if (!connected)
            return {false, "scripted TCP transport disconnected"};
        if (failNextSend)
        {
            failNextSend = false;
            return {false, "scripted TCP send failure"};
        }
        lastText = text;
        ++sendCount;
        return {true, "sent without response"};
    }

    bool connectSucceeds = true;
    bool connected = false;
    bool failNextSend = false;
    int sendCount = 0;
    std::string lastAddress;
    std::uint16_t lastPort = 0;
    int lastTimeoutMs = 0;
    std::string lastText;
};

struct TestOpcUaAdapter final : IOpcUaAdapter
{
    const char* AdapterName() const override { return "test-opcua"; }
    DeviceOperationResult Connect(const DeviceEndpoint&) override
    {
        state = DeviceConnectionState::Connected;
        return {true, {}};
    }
    void Disconnect() override { state = DeviceConnectionState::Disconnected; }
    DeviceConnectionState ConnectionState() const override { return state; }
    std::string LastError() const override { return {}; }
    DeviceOperationResult ReadNode(const std::string&, DeviceValue& value) override
    {
        value = false;
        return {true, {}};
    }
    DeviceOperationResult WriteNode(const std::string& nodeId, const DeviceValue& value) override
    {
        lastNodeId = nodeId;
        lastValue = value;
        return {true, {}};
    }

    DeviceConnectionState state = DeviceConnectionState::Disconnected;
    std::string lastNodeId;
    DeviceValue lastValue = false;
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

void TestRotatedROIExtractionAndResultRestore()
{
    cv::Mat source = cv::Mat::zeros(160, 180, CV_8UC3);
    ROI roi;
    roi.type = ROI_TYPE_RECT;
    roi.start = ImVec2(55.0f, 60.0f);
    roi.end = ImVec2(125.0f, 100.0f);
    roi.angle = 30.0f;

    const auto corners = roi.Corners();
    std::vector<cv::Point> polygon;
    for (const ImVec2& point : corners)
        polygon.emplace_back(cvRound(point.x), cvRound(point.y));
    cv::fillConvexPoly(source, polygon, cv::Scalar(40, 120, 220));

    cv::Mat crop;
    RotatedROI::Transform transform;
    Require(RotatedROI::Extract(source, roi, crop, transform),
        "rotated ROI extraction failed");
    Require(crop.size() == cv::Size(70, 40), "rotated ROI crop size regressed");
    Require(cv::mean(crop)[2] > 180.0, "rotated ROI crop sampled the wrong image area");

    ToolResult result;
    ToolResult::Region region;
    region.bbox = cv::Rect(10, 8, 20, 12);
    region.contour = {{10, 8}, {30, 8}, {30, 20}, {10, 20}};
    region.center = cv::Point2f(20.0f, 14.0f);
    region.area = 240.0f;
    region.angle = 5.0f;
    result.regions.push_back(region);
    result.detections.push_back({cv::Rect(12, 10, 9, 7), 2, 0.8f, "part"});
    result.lines.push_back({cv::Point(0, 20), cv::Point(70, 20), 70.0f, 0.0f});
    result.texts.push_back({"code", cv::Rect(15, 6, 18, 8), 0.9f});
    result.debugImage = crop.clone();

    const cv::Point2f expectedCenter = RotatedROI::MapPoint(
        cv::Point2f(20.0f, 14.0f), transform.cropToSource);
    RotatedROI::RestoreResult(result, transform);
    Require(cv::norm(result.regions[0].center - expectedCenter) < 0.01,
        "rotated ROI region center restore regressed");
    Require(std::abs(result.regions[0].angle - 35.0f) < 0.01f,
        "rotated ROI region angle restore regressed");
    Require(result.regions[0].bbox.contains(result.regions[0].contour.front()),
        "rotated ROI region bounds restore regressed");
    Require(result.detections[0].box.area() > 0 && result.texts[0].box.area() > 0,
        "rotated ROI box restore regressed");
    Require(std::abs(result.lines[0].angle - 30.0f) < 1.0f,
        "rotated ROI line angle restore regressed");

    cv::Mat restoredDebug;
    Require(RotatedROI::RestoreDebugImage(result.debugImage, source, transform, restoredDebug),
        "rotated ROI debug image restore failed");
    Require(restoredDebug.size() == source.size() && restoredDebug.type() == source.type(),
        "rotated ROI debug image shape regressed");
    const cv::Rect bounds = roi.ToCvRect();
    for (const ImVec2& point : corners)
        Require(bounds.contains(cv::Point(static_cast<int>(std::floor(point.x)),
                                          static_cast<int>(std::floor(point.y)))),
            "rotated ROI covering rectangle regressed");
}

void TestToolExecutorUsesRotatedROI()
{
    VisionContext context;
    context.image = cv::Mat::zeros(180, 200, CV_8UC3);
    cv::circle(context.image, cv::Point(100, 90), 9, cv::Scalar(255, 255, 255), cv::FILLED);
    context.originalImage = context.image;
    context.width = context.image.cols;
    context.height = context.image.rows;
    ROI roi;
    roi.type = ROI_TYPE_RECT;
    roi.start = ImVec2(60.0f, 65.0f);
    roi.end = ImVec2(140.0f, 115.0f);
    roi.angle = 32.0f;
    context.rois.push_back(roi);
    context.selectedROI = 0;

    ToolInstance tool;
    tool.type = 2;
    tool.toolId = 7001;
    tool.blobThresholdMode = 1;
    tool.blobThreshold = 127;
    tool.blobMinArea = 50;
    tool.blobMaxArea = 1000;
    ToolExecutor::RunViaITool(tool, context);
    Require(tool.hasLastResult && tool.lastResult.success &&
        tool.lastResult.regions.size() == 1,
        "ToolExecutor rotated ROI Blob execution failed");
    Require(cv::norm(tool.lastResult.regions[0].center - cv::Point2f(100.0f, 90.0f)) < 2.0,
        "ToolExecutor rotated ROI result coordinates regressed");
}

void TestGeometryDrawToolAndRecipe()
{
    GeometryPrimitive line;
    line.type = GeometryPrimitiveType::Line;
    line.name = "axis";
    line.points = {{10.0f, 12.0f}, {90.0f, 40.0f}};

    GeometryPrimitive rotated;
    rotated.type = GeometryPrimitiveType::RotatedRectangle;
    rotated.name = "rotated";
    rotated.points = {{35.0f, 30.0f}, {95.0f, 70.0f}};
    rotated.angle = 27.5f;
    rotated.filled = true;
    rotated.color = {255, 80, 20, 128};

    GeometryPrimitive text;
    text.type = GeometryPrimitiveType::Text;
    text.name = "caption";
    text.text = "测试 A";
    text.points = {{8.0f, 80.0f}};
    text.fontSize = 18;

    VisionContext context;
    context.image = cv::Mat::zeros(110, 130, CV_8UC3);
    GeometryDrawTool tool;
    tool.primitives = {line, rotated, text};
    ToolResult result = tool.Execute(context);
    Require(result.success && result.debugImage.size() == context.image.size(),
        "geometry draw output image regressed");
    Require(result.lines.size() == 1 && result.regions.size() == 1 && result.texts.size() == 1,
        "geometry draw result contract regressed");
    Require(cv::countNonZero(result.debugImage.reshape(1)) > 0,
        "geometry draw produced a blank image");

    ToolInstance instance;
    instance.type = 17;
    instance.geometryDrawType = static_cast<int>(GeometryPrimitiveType::RotatedRectangle);
    instance.geometryItems = {line, rotated, text};
    ToolInstance loaded;
    loaded.LoadRecipeJson(instance.ToRecipeJson());
    Require(loaded.type == 17 && loaded.geometryItems.size() == 3 &&
        loaded.geometryDrawType == static_cast<int>(GeometryPrimitiveType::RotatedRectangle) &&
        std::abs(loaded.geometryItems[1].angle - 27.5f) < 0.001f &&
        loaded.geometryItems[2].text == "测试 A",
        "geometry draw recipe serialization regressed");
    Require(ITool::Create(17) != nullptr, "geometry draw tool registration regressed");
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

void TestImageImportServiceAndDecodeFailures()
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "imgui_opencv_image_import_regression";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "nested");

    const std::filesystem::path validRoot = root / "valid.png";
    const std::filesystem::path validNested = root / "nested" / "valid.jpg";
    const std::filesystem::path invalid = root / "invalid.png";
    cv::Mat sample(12, 16, CV_8UC3, cv::Scalar(10, 20, 30));
    Require(cv::imwrite(validRoot.string(), sample), "image import fixture write failed");
    Require(cv::imwrite(validNested.string(), sample), "nested image import fixture write failed");
    std::ofstream invalidStream(invalid, std::ios::binary);
    invalidStream << "not an image";
    invalidStream.close();

    ToolChainState::ClearTools();
    ToolInstance runtimeTool;
    runtimeTool.type = 2;
    ToolChainState::AddTool(std::move(runtimeTool));
    ROIState::Add(ROI{}, true);
    gContext.unifiedResults.push_back(ToolResult{});

    ImageImportResult folder = ImageImportService::ImportFolder(root.string(), true);
    Require(folder.success && folder.imageCount == 3 && folder.imageIndex == 0,
        "image folder import plan did not include supported recursive files");
    Require(FrameNavigation::ImageList().size() == 3,
        "image folder import did not update frame navigation");
    Require(ROIState::ReadOnlyItems().empty() && gContext.unifiedResults.empty(),
        "image folder import did not clear stale inspection state");
    std::string pendingImagePath;
    Require(FrameNavigation::ConsumePendingImagePath(pendingImagePath) && pendingImagePath == folder.imagePath,
        "image folder import did not submit the first image request");

    ImageImportResult next = ImageImportService::NavigateNextImage();
    Require(next.success && next.imageIndex == 1 &&
        FrameNavigation::ConsumePendingImagePath(pendingImagePath) && pendingImagePath == next.imagePath,
        "image navigation service did not submit the next image request");

    const ImageImportResult empty = ImageImportService::ImportFolder((root / "missing").string(), true);
    Require(!empty.success && empty.message.find("不存在") != std::string::npos,
        "missing image folder was not reported clearly");
    const std::filesystem::path emptyFolder = root / "empty";
    std::filesystem::create_directories(emptyFolder);
    const ImageImportResult noImages = ImageImportService::ImportFolder(emptyFolder.string(), true);
    Require(!noImages.success && noImages.message.find("没有找到") != std::string::npos &&
        FrameNavigation::ImageList().empty(),
        "empty image folder was not reported and cleared");

    const ImageImportResult single = ImageImportService::ImportSingleImage(validRoot.string());
    Require(single.success && FrameNavigation::ImageList().empty() &&
        FrameNavigation::ConsumePendingImagePath(pendingImagePath) && pendingImagePath == validRoot.string(),
        "single image import did not switch away from folder mode");
    const ImageImportResult missingFile = ImageImportService::ImportSingleImage(
        (root / "missing.jpg").string());
    Require(!missingFile.success && missingFile.message.find("不存在") != std::string::npos,
        "missing image file was not reported clearly");

    FrameSourceState::SetCurrentFrame(sample, FrameSourceType::SingleImage, validRoot.string(), 0, 0.0);
    FrameNavigation::SetImageList({validRoot.string()});
    ROIState::Add(ROI{}, true);
    gContext.unifiedResults.push_back(ToolResult{});
    ImageImportService::ClearCurrentInput();
    Require(!ImageState::HasImage() && !FrameSourceState::HasFrame() &&
        FrameNavigation::ImageList().empty() && ROIState::ReadOnlyItems().empty() &&
        gContext.unifiedResults.empty(),
        "clear current input left stale image, navigation, ROI, or result state");

    const FrameNavigation::PlaybackState playback = FrameNavigation::CurrentPlayback();
    Require(!playback.open && !playback.playing && !playback.camera &&
        playback.frameCount == 0 && playback.currentFrame == 0,
        "closed playback snapshot returned stale state");

    AsyncImageLoader::Cancel();
    AsyncImageLoader::RequestLoad(validRoot.string());
    bool loaded = false;
    bool loadFailed = false;
    for (int attempt = 0; attempt < 400 && !loaded; ++attempt)
    {
        AsyncImageLoader::CheckAndProcess(
            [&](cv::Mat image)
            {
                loaded = !image.empty() && image.size() == sample.size();
            },
            [&](const std::string&)
            {
                loadFailed = true;
            });
        if (!loaded && !loadFailed)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Require(loaded && !loadFailed, "valid image async decode did not complete");

    AsyncImageLoader::RequestLoad(invalid.string());
    bool failed = false;
    std::string failureMessage;
    for (int attempt = 0; attempt < 400 && !failed; ++attempt)
    {
        AsyncImageLoader::CheckAndProcess(
            [&](cv::Mat)
            {
                failed = true;
            },
            [&](const std::string& message)
            {
                failed = true;
                failureMessage = message;
            });
        if (!failed)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Require(failed && failureMessage.find("无法读取或解码图片") != std::string::npos,
        "invalid image async decode did not report a clear failure");

    ToolChainState::ClearTools();
    FrameNavigation::SetImageList({});
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
    settings.measurementRangeEnabled = true;
    settings.measurementName = "blobMeanCircularity";
    settings.minMeasurement = 0.75;
    settings.maxMeasurement = 0.95;

    ToolResult result;
    result.success = true;
    result.texts.push_back({"station pass", cv::Rect(0, 0, 20, 20), 0.9f});
    result.texts.push_back({"station pass 2", cv::Rect(20, 0, 20, 20), 0.85f});
    result.measurements.push_back({"blobMeanCircularity", 0.82, "ratio"});
    ToolJudgement::Evaluate(result, settings);
    Require(result.status == ToolResultStatus::Pass, "matching judgement should pass");
    Require(!ToolJudgement::ShouldStop(result, settings), "pass result should not stop chain");

    result.texts.resize(1);
    ToolJudgement::Evaluate(result, settings);
    Require(result.status == ToolResultStatus::Fail, "result count judgement should fail");
    Require(ToolJudgement::ShouldStop(result, settings), "configured fail should stop chain");

    result.texts.push_back({"station pass 2", cv::Rect(20, 0, 20, 20), 0.85f});
    result.measurements[0].value = 0.60;
    ToolJudgement::Evaluate(result, settings);
    Require(result.status == ToolResultStatus::Fail &&
        result.statusReason.find("blobMeanCircularity") != std::string::npos,
        "measurement range judgement should report the failed metric");

    result.measurements.clear();
    ToolJudgement::Evaluate(result, settings);
    Require(result.status == ToolResultStatus::Fail &&
        result.statusReason.find("未找到测量项") != std::string::npos,
        "missing measurement judgement should report a clear reason");

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

    distortion.k1 = 0.10;
    distortion.k2 = -0.02;
    distortion.p1 = 0.001;
    distortion.p2 = -0.001;
    distortion.k3 = 0.005;
    const cv::Point2d idealPixel(500.0, 350.0);
    const double normalizedX = (idealPixel.x - distortion.cx) / distortion.fx;
    const double normalizedY = (idealPixel.y - distortion.cy) / distortion.fy;
    const double radius2 = normalizedX * normalizedX + normalizedY * normalizedY;
    const double radial = 1.0 + distortion.k1 * radius2 +
        distortion.k2 * radius2 * radius2 + distortion.k3 * radius2 * radius2 * radius2;
    const double distortedX = normalizedX * radial +
        2.0 * distortion.p1 * normalizedX * normalizedY +
        distortion.p2 * (radius2 + 2.0 * normalizedX * normalizedX);
    const double distortedY = normalizedY * radial +
        distortion.p1 * (radius2 + 2.0 * normalizedY * normalizedY) +
        2.0 * distortion.p2 * normalizedX * normalizedY;
    const cv::Point2d observedPixel(
        distortion.fx * distortedX + distortion.cx,
        distortion.fy * distortedY + distortion.cy);
    Require(cv::norm(distortion.UndistortPixel(observedPixel) - idealPixel) < 1.0e-4,
        "non-zero radial/tangential lens distortion correction regressed");
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

    request.mode = ResultROIMode::NthResult;
    request.resultIndex = 0;
    request.category = "B";
    request.classId = 1;
    request.minScore = 0.75f;
    resolution = ResultROIResolver::Resolve(source, request);
    Require(resolution.available && resolution.rois[0].ToCvRect() == cv::Rect(50, 60, 20, 10),
        "result ROI category/class filter regressed");

    request.category.clear();
    request.classId = -1;
    request.minScore = -1.0f;
    request.sortMode = 2;
    request.sortDescending = true;
    request.resultIndex = 1;
    resolution = ResultROIResolver::Resolve(source, request);
    Require(resolution.available && resolution.rois[0].ToCvRect() == cv::Rect(50, 60, 20, 10),
        "result ROI area sorting regressed");

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
    ToolChainState::EnsureToolIds();
    const std::uint64_t detectorId = tools[0].toolId;
    tools[1].resultRoiSourceToolId = detectorId;
    tools[1].fixture.sourceToolId = detectorId;
    ToolChainState::SetActiveIndex(1);
    ToolChainState::SetYoloLiveInstanceIndex(0);

    ToolChainState::MoveOriginalToolToFront();
    Require(tools[0].type == 12 && tools[2].type == 2,
        "original tool reorder regressed");
    Require(tools[2].resultRoiSourceTool == 1,
        "result ROI source index was not remapped after reorder");
    Require(tools[2].fixture.sourceToolIndex == 1,
        "fixture source index was not remapped after reorder");
    Require(tools[2].resultRoiSourceToolId == detectorId &&
        tools[2].fixture.sourceToolId == detectorId,
        "stable tool identity changed after reorder");
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
    data.rois.push_back({1.0f, 2.0f, 30.0f, 40.0f, 27.5f, 0});

    ToolInstance tool;
    tool.type = 4;
    tool.toolId = 1001;
    tool.enabled = false;
    tool.label = "定位A";
    tool.skipIfModelMissing = true;
    tool.groupName = "检测组";
    tool.collapsed = true;
    tool.yoloModelPath = "models/yolo.onnx";
    tool.yoloClassesPath = "models/classes.txt";
    tool.yoloConfThreshold = 0.67f;
    tool.yoloNmsThreshold = 0.45f;
    tool.yoloUseROI = true;
    tool.differenceThreshold = 41;
    tool.differenceMinArea = 33;
    tool.differenceMorphKernelSize = 5;
    tool.differenceShowLabels = false;
    tool.judgement.enabled = true;
    tool.judgement.stopOnFailure = true;
    tool.judgement.minResultCount = 2;
    tool.judgement.maxResultCount = 5;
    tool.judgement.minScore = 0.75f;
    tool.judgement.measurementRangeEnabled = true;
    tool.judgement.measurementName = "blobMeanCircularity";
    tool.judgement.minMeasurement = 0.7;
    tool.judgement.maxMeasurement = 0.95;
    tool.judgement.requiredText = "target";
    tool.resultRoiMode = 1;
    tool.resultRoiSourceTool = 0;
    tool.resultRoiSourceToolId = 1001;
    tool.resultRoiIndex = 2;
    tool.resultRoiMissingPolicy = 1;
    tool.resultRoiCategory = "A";
    tool.resultRoiClassId = 7;
    tool.resultRoiMinScore = 0.82f;
    tool.resultRoiMinArea = 120.0f;
    tool.resultRoiSortMode = 2;
    tool.resultRoiSortDescending = false;
    ROI toolROI;
    toolROI.type = ROI_TYPE_RECT;
    toolROI.start = ImVec2(5.0f, 6.0f);
    toolROI.end = ImVec2(20.0f, 21.0f);
    toolROI.angle = -12.25f;
    tool.searchROIs.push_back(toolROI);
    RecipeToolInstance toolSnapshot;
    toolSnapshot.CaptureFrom(tool);
    toolSnapshot.templateFile = "imgui_opencv_regression_tool.png";
    toolSnapshot.templateImage = cv::Mat(2, 3, CV_8UC3, cv::Scalar(31, 37, 41)).clone();
    toolSnapshot.differenceReferenceFile = "imgui_opencv_regression_difference.png";
    toolSnapshot.differenceReferenceImage = cv::Mat(5, 6, CV_8UC3,
        cv::Scalar(43, 47, 53)).clone();
    data.tools.push_back(std::move(toolSnapshot));

    ToolInstance mcf;
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
    mcf.mcfRefImage = cv::Mat(3, 4, CV_8UC3, cv::Scalar(7, 11, 13)).clone();
    RecipeToolInstance mcfSnapshot;
    mcfSnapshot.CaptureFrom(mcf);
    mcfSnapshot.mcfPointsJson = R"({"points":[{"x":1,"y":2,"b":3,"g":4,"r":5,"tolerance":6}]})";
    const std::string expectedMcfPoints = mcfSnapshot.mcfPointsJson;
    data.tools.push_back(std::move(mcfSnapshot));

    ToolInstance qr;
    qr.type = 14;
    qr.qrUseROI = false;
    qr.qrDetectMulti = false;
    qr.qrEnhance = false;
    qr.qrMinSize = 37;
    qr.qrShowText = false;
    qr.qrEngine = 2;
    qr.qrFormatMask = BarcodeFormatCode128 | BarcodeFormatDataMatrix;
    qr.qrFilterDuplicates = false;
    RecipeToolInstance qrSnapshot;
    qrSnapshot.CaptureFrom(qr);
    data.tools.push_back(std::move(qrSnapshot));

    ToolInstance measurement;
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
    measurement.measureCalibrationSamples.push_back({{0.0, 0.0}, {10.0, 20.0}});
    measurement.measureCalibrationSamples.push_back({{100.0, 0.0}, {30.0, 20.0}});
    measurement.fixture.enabled = true;
    measurement.fixture.sourceToolIndex = 0;
    measurement.fixture.referenceOrigin = {12.0f, 34.0f};
    measurement.fixture.referenceAngleDegrees = 5.0f;
    measurement.measureToleranceEnabled = true;
    measurement.measureNominal = 12.0f;
    measurement.measureToleranceMinus = 0.1f;
    measurement.measureTolerancePlus = 0.2f;
    RecipeToolInstance measurementSnapshot;
    measurementSnapshot.CaptureFrom(measurement);
    data.tools.push_back(std::move(measurementSnapshot));

    std::filesystem::path path = std::filesystem::temp_directory_path() / "imgui_opencv_regression.recipe";
    std::filesystem::remove(path);

    Require(RecipeManager::Save(path.string().c_str(), data), "recipe save failed");

    RecipeData loaded;
    Require(RecipeManager::Load(path.string().c_str(), loaded), "recipe load failed");

    Require(loaded.name == data.name, "recipe name round-trip regressed");
    Require(loaded.threshold.useGray == data.threshold.useGray, "threshold round-trip regressed");
    Require(loaded.threshold.thresholdValue == data.threshold.thresholdValue, "threshold value round-trip regressed");
    Require(loaded.rois.size() == 1 && loaded.rois[0].endX == 30.0f &&
        std::abs(loaded.rois[0].angle - 27.5f) < 0.001f, "ROI round-trip regressed");
    Require(loaded.tools.size() == 4, "tool count round-trip regressed");
    const ToolInstance loadedTool = loaded.tools[0].CreateToolInstance();
    const ToolInstance loadedMcf = loaded.tools[1].CreateToolInstance();
    const ToolInstance loadedQr = loaded.tools[2].CreateToolInstance();
    const ToolInstance loadedMeasurement = loaded.tools[3].CreateToolInstance();
    Require(loadedTool.type == 4, "YOLO tool type round-trip regressed");
    Require(loadedTool.toolId == 1001 && loadedTool.resultRoiSourceToolId == 1001,
        "stable tool identity recipe round-trip regressed");
    Require(!loadedTool.enabled, "tool enabled flag recipe round-trip regressed");
    Require(loadedTool.differenceThreshold == 41 && loadedTool.differenceMinArea == 33 &&
        loadedTool.differenceMorphKernelSize == 5 && !loadedTool.differenceShowLabels,
        "difference recipe fields round-trip regressed");
    Require(loadedTool.templateImg.size() == cv::Size(3, 2) &&
        loadedTool.templateImg.at<cv::Vec3b>(0, 0) == cv::Vec3b(31, 37, 41),
        "recipe template asset snapshot round-trip regressed");
    Require(loadedTool.differenceReferenceImage.size() == cv::Size(6, 5) &&
        loadedTool.differenceReferenceImage.at<cv::Vec3b>(0, 0) == cv::Vec3b(43, 47, 53),
        "recipe difference asset snapshot round-trip regressed");
    Require(loadedTool.label == "定位A", "tool label round-trip regressed");
    Require(loadedTool.skipIfModelMissing, "missing-model skip policy recipe round-trip regressed");
    Require(loadedTool.groupName == "检测组" && loadedTool.collapsed,
        "tool group/collapse recipe round-trip regressed");
    Require(loadedTool.yoloUseROI, "YOLO ROI flag round-trip regressed");
    Require(loadedTool.searchROIs.size() == 1 &&
        std::abs(loadedTool.searchROIs[0].angle + 12.25f) < 0.001f,
        "tool ROI angle round-trip regressed");
    Require(loadedTool.judgement.enabled && loadedTool.judgement.stopOnFailure,
        "judgement flags round-trip regressed");
    Require(loadedTool.judgement.minResultCount == 2 && loadedTool.judgement.maxResultCount == 5,
        "judgement count round-trip regressed");
    Require(std::abs(loadedTool.judgement.minScore - 0.75f) < 0.001f &&
        loadedTool.judgement.requiredText == "target",
        "judgement conditions round-trip regressed");
    Require(loadedTool.judgement.measurementRangeEnabled &&
        loadedTool.judgement.measurementName == "blobMeanCircularity" &&
        std::abs(loadedTool.judgement.minMeasurement - 0.7) < 0.000001 &&
        std::abs(loadedTool.judgement.maxMeasurement - 0.95) < 0.000001,
        "measurement judgement recipe round-trip regressed");
    Require(std::abs(loadedTool.yoloConfThreshold - 0.67f) < 0.001f,
        "YOLO confidence round-trip regressed");
    Require(loadedTool.resultRoiMode == 1 && loadedTool.resultRoiSourceTool == 0 &&
        loadedTool.resultRoiIndex == 2 && loadedTool.resultRoiMissingPolicy == 1,
        "result ROI settings round-trip regressed");
    Require(loadedTool.resultRoiCategory == "A" && loadedTool.resultRoiClassId == 7 &&
        std::abs(loadedTool.resultRoiMinScore - 0.82f) < 0.0001f &&
        std::abs(loadedTool.resultRoiMinArea - 120.0f) < 0.0001f &&
        loadedTool.resultRoiSortMode == 2 && !loadedTool.resultRoiSortDescending,
        "result ROI filtering/sorting recipe fields regressed");
    Require(loadedMcf.type == 10, "multi-color tool type round-trip regressed");
    Require(loadedMcf.mcfUseROI, "multi-color ROI flag round-trip regressed");
    Require(loadedMcf.mcfMaxResults == 7, "multi-color max results round-trip regressed");
    Require(std::abs(loadedMcf.mcfMinDist - 11.5f) < 0.001f, "multi-color min distance round-trip regressed");
    Require(loadedMcf.mcfCrossSize == 13, "multi-color cross size round-trip regressed");
    Require(loadedMcf.mcfCrossThick == 4, "multi-color cross thickness round-trip regressed");
    Require(loadedMcf.mcfAnchorX == 21 && loadedMcf.mcfAnchorY == 22, "multi-color anchor round-trip regressed");
    Require(loadedMcf.mcfImgGray && loadedMcf.mcfImgBinary, "multi-color preprocess flags round-trip regressed");
    Require(loadedMcf.mcfImgBinThresh == 173, "multi-color binary threshold round-trip regressed");
    Require(loadedMcf.mcfRoiX == 2 && loadedMcf.mcfRoiH == 41, "multi-color ROI rect round-trip regressed");
    Require(!loadedMcf.mcfRefImage.empty() && loadedMcf.mcfRefImage.size() == cv::Size(4, 3),
        "multi-color reference image round-trip regressed");
    Require(loaded.tools[1].mcfPointsJson == expectedMcfPoints,
        "multi-color points round-trip regressed");
    Require(loadedQr.type == 14, "QR tool type round-trip regressed");
    Require(!loadedQr.qrUseROI && !loadedQr.qrDetectMulti && !loadedQr.qrEnhance,
        "QR boolean parameters round-trip regressed");
    Require(loadedQr.qrMinSize == 37 && loadedQr.qrEngine == 2,
        "QR numeric parameters round-trip regressed");
    Require(!loadedQr.qrShowText, "QR label flag round-trip regressed");
    Require(loadedQr.qrFormatMask == (BarcodeFormatCode128 | BarcodeFormatDataMatrix),
        "barcode format filter round-trip regressed");
    Require(!loadedQr.qrFilterDuplicates, "barcode duplicate filter round-trip regressed");
    Require(loadedMeasurement.type == 15 && loadedMeasurement.measureMode == 3,
        "measurement tool recipe type regressed");
    Require(loadedMeasurement.measureCalibrationSamples.size() == 2 &&
        std::abs(loadedMeasurement.measureCalibrationSamples[1].world.x - 30.0) < 0.001,
        "calibration sample recipe round-trip regressed");
    Require(std::abs(loadedMeasurement.measureMmPerPixel - 0.025f) < 0.0001f &&
        loadedMeasurement.measureToleranceEnabled &&
        std::abs(loadedMeasurement.measureTolerancePlus - 0.2f) < 0.0001f,
        "measurement calibration/tolerance round-trip regressed");
    Require(loadedMeasurement.measureCaliperCount == 24 &&
        loadedMeasurement.measureEdgePolarity == 2 &&
        std::abs(loadedMeasurement.measureMinimumConfidence - 0.75f) < 0.0001f,
        "measurement caliper parameters round-trip regressed");
    Require(loadedMeasurement.measureCalibration.enabled &&
        loadedMeasurement.measureCalibration.homographyEnabled &&
        loadedMeasurement.measureCalibration.distortionEnabled &&
        std::abs(loadedMeasurement.measureCalibration.scaleY - 0.03) < 1.0e-9 &&
        std::abs(loadedMeasurement.measureCalibration.pixelToWorldHomography(0, 2) - 4.5) < 1.0e-9,
        "full calibration round-trip regressed");
    Require(loadedMeasurement.fixture.enabled && loadedMeasurement.fixture.sourceToolIndex == 0 &&
        cv::norm(loadedMeasurement.fixture.referenceOrigin - cv::Point2f(12.0f, 34.0f)) < 0.001f,
        "fixture settings round-trip regressed");

    const std::filesystem::path legacyPath =
        std::filesystem::temp_directory_path() / "imgui_opencv_legacy_roi.recipe";
    {
        std::ifstream input(path);
        nlohmann::json legacy = nlohmann::json::parse(input);
        legacy["rois"][0].erase("angle");
        legacy["tools"][0]["searchROIs"][0].erase("angle");
        std::ofstream output(legacyPath);
        output << legacy.dump(2);
    }
    RecipeData legacyLoaded;
    Require(RecipeManager::Load(legacyPath.string().c_str(), legacyLoaded),
        "legacy ROI recipe load failed");
    const ToolInstance legacyTool = legacyLoaded.tools[0].CreateToolInstance();
    Require(legacyLoaded.rois.size() == 1 && legacyLoaded.rois[0].angle == 0.0f &&
        legacyTool.searchROIs.size() == 1 && legacyTool.searchROIs[0].angle == 0.0f,
        "legacy recipe ROI angle did not default to zero");

    std::filesystem::remove(path);
    std::filesystem::remove(legacyPath);
    std::filesystem::remove(path.parent_path() / "imgui_opencv_regression_tool.png");
    std::filesystem::remove(path.parent_path() / "imgui_opencv_regression_difference.png");
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
    tool.thresholdMode = 1;
    tool.threshold = 128;
    tool.minCircularity = 0.5f;
    tool.maxCircularity = 1.0f;
    tool.minAspectRatio = 1.0f;
    tool.maxAspectRatio = 2.0f;

    ITool* entry = &tool;
    ToolResult result = entry->Execute(ctx);
    Require(result.success, "blob ITool execution failed");
    Require(result.regions.size() == 2, "blob ITool region count regressed");
    Require(result.regions[0].area >= 40.0f, "blob ITool area regressed");
    Require(result.regions[0].center.x > 0.0f && result.regions[0].center.y > 0.0f,
        "blob ITool center regressed");
    Require(result.regions[0].circularity > 0.5f && result.regions[0].aspectRatio >= 1.0f,
        "blob ITool shape descriptors regressed");
    Require(result.regions[0].contour.size() >= 4, "blob ITool contour regressed");
    auto findMeasurement = [&result](const char* name)
    {
        for (const auto& measurement : result.measurements)
            if (measurement.name == name)
                return measurement.value;
        return -1.0;
    };
    Require(findMeasurement("blobCandidateCount") >= 2.0,
        "blob candidate count metric regressed");
    Require(findMeasurement("blobValidCount") == 2.0,
        "blob valid count metric regressed");
    Require(findMeasurement("blobMeanArea") > 40.0,
        "blob mean area metric regressed");
    Require(findMeasurement("blobMeanCircularity") > 0.5,
        "blob mean circularity metric regressed");
}

void TestToolInstanceOwnsRecipeSerialization()
{
    ToolInstance source;
    source.type = 15;
    source.toolId = 998877;
    source.enabled = false;
    source.label = "尺寸A";
    source.showResultLabels = false;
    source.showTemplatePreview = false;
    source.resultRoiMode = 2;
    source.resultRoiSourceToolId = 1234;
    source.fixture.enabled = true;
    source.fixture.sourceToolId = 5678;
    source.fixture.referenceOrigin = {12.5f, 8.25f};
    source.judgement.enabled = true;
    source.judgement.stopOnFailure = true;
    source.judgement.minResultCount = 2;
    source.measureMode = 5;
    source.measureCaliperCount = 24;
    source.measureMinimumConfidence = 0.82f;
    source.measureCalibration.enabled = true;
    source.measureCalibration.scaleX = 0.02;
    source.measureCalibration.scaleY = 0.03;
    source.measureCalibrationSamples.push_back({{1.0, 2.0}, {3.0, 4.0}});

    ROI polygon;
    polygon.type = ROI_TYPE_POLYGON;
    polygon.start = {1.0f, 2.0f};
    polygon.end = {20.0f, 30.0f};
    polygon.points = {{1.0f, 2.0f}, {20.0f, 2.0f}, {10.0f, 30.0f}};
    source.searchROIs.push_back(polygon);

    const nlohmann::json serialized = source.ToRecipeJson();
    ToolInstance loaded;
    loaded.LoadRecipeJson(serialized);

    Require(loaded.type == source.type && loaded.toolId == source.toolId && !loaded.enabled,
        "ToolInstance identity recipe serialization regressed");
    Require(loaded.label == source.label && !loaded.showResultLabels && !loaded.showTemplatePreview,
        "ToolInstance display recipe serialization regressed");
    Require(loaded.fixture.sourceToolId == 5678 && loaded.judgement.stopOnFailure,
        "ToolInstance dependency/judgement serialization regressed");
    Require(loaded.searchROIs.size() == 1 && loaded.searchROIs[0].points.size() == 3,
        "ToolInstance polygon ROI serialization regressed");
    Require(loaded.measureCaliperCount == 24 &&
        std::abs(loaded.measureMinimumConfidence - 0.82f) < 0.0001f &&
        loaded.measureCalibrationSamples.size() == 1,
        "ToolInstance measurement serialization regressed");

    loaded.hasLastResult = true;
    loaded.measureRuntimeROIIds.push_back(42);
    loaded.ClearRuntimeState();
    Require(!loaded.hasLastResult && loaded.measureRuntimeROIIds.empty(),
        "ToolInstance runtime state cleanup regressed");
}

void TestDifferenceToolITool()
{
    VisionContext ctx;
    ctx.image = cv::Mat::zeros(80, 80, CV_8UC1);
    cv::rectangle(ctx.image, cv::Rect(30, 25, 12, 10), cv::Scalar(200), cv::FILLED);

    DifferenceTool tool;
    tool.referenceImage = cv::Mat::zeros(80, 80, CV_8UC1);
    tool.threshold = 20;
    tool.minArea = 20;
    tool.morphKernelSize = 3;
    tool.morphIterations = 1;

    ToolResult result = tool.Execute(ctx);
    Require(result.success, "difference ITool execution failed");
    Require(result.regions.size() == 1, "difference region count regressed");
    Require(result.regions[0].area >= 20.0f && result.regions[0].bbox.x == 30,
        "difference region geometry regressed");
    Require(result.measurements.size() >= 2 && !result.debugImage.empty(),
        "difference measurements/debug image regressed");
}

void TestROIEditorStateOwnsInteraction()
{
    ROIEditorState::ResetInteraction();
    ROIEditorState::BeginDrawSequence({ROI_TYPE_POINT, ROI_TYPE_LINE});
    Require(ROIEditorState::IsDrawSequenceActive() &&
        ROIEditorState::CurrentROIType() == ROI_TYPE_POINT,
        "ROI editor sequence did not start in Core state");

    ROI point;
    point.type = ROI_TYPE_POINT;
    ROIEditorState::EnsureRuntimeId(point);
    const std::uint64_t pointId = point.runtimeId;
    ROIEditorState::AdvanceDrawSequence(point);
    Require(ROIEditorState::DrawSequenceStep() == 1 &&
        ROIEditorState::CurrentROIType() == ROI_TYPE_LINE,
        "ROI editor sequence did not advance in Core state");

    ROI line;
    line.type = ROI_TYPE_LINE;
    ROIEditorState::AdvanceDrawSequence(line);
    std::vector<ROI> completed;
    Require(ROIEditorState::ConsumeCompletedDrawSequence(completed) &&
        completed.size() == 2 && completed[0].runtimeId == pointId,
        "ROI editor completed sequence contract regressed");
    ROIEditorState::ResetInteraction();
}

void TestToolAssetServiceOwnsCaptureWorkflow()
{
    ToolAssetService::ClearSessions();
    ROIState::ClearInteraction();

    cv::Mat image(80, 100, CV_8UC3);
    for (int y = 0; y < image.rows; ++y)
    {
        for (int x = 0; x < image.cols; ++x)
            image.at<cv::Vec3b>(y, x) = cv::Vec3b(x, y, x + y);
    }
    ImageState::SetImage(image);

    ToolInstance tool;
    tool.toolId = 9001;
    int roiIndex = ToolAssetService::BeginROICapture(tool, ToolAssetKind::TemplateMatch);
    Require(ROIState::IsValidIndex(roiIndex), "asset capture did not create an editable ROI");
    ROI* roi = ROIState::MutableAt(roiIndex);
    Require(roi != nullptr, "asset capture ROI lookup failed");
    roi->start = ImVec2(10.0f, 12.0f);
    roi->end = ImVec2(30.0f, 32.0f);

    ToolAssetCaptureResult capture =
        ToolAssetService::ConfirmROICapture(tool, ToolAssetKind::TemplateMatch);
    Require(capture.success && tool.templateImg.size() == cv::Size(20, 20) &&
        tool.hasTemplateROI && ROIState::Items().empty(),
        "template asset capture workflow regressed");
    Require(tool.templateImg.at<cv::Vec3b>(0, 0) == image.at<cv::Vec3b>(12, 10),
        "template asset pixels were captured from the wrong coordinates");

    roiIndex = ToolAssetService::BeginROICapture(tool, ToolAssetKind::ShapeTemplate);
    roi = ROIState::MutableAt(roiIndex);
    Require(roi != nullptr, "shape capture ROI was not created");
    roi->start = ImVec2(20.0f, 15.0f);
    roi->end = ImVec2(45.0f, 40.0f);
    ROI unrelated;
    unrelated.type = ROI_TYPE_RECT;
    unrelated.start = ImVec2(1.0f, 1.0f);
    unrelated.end = ImVec2(3.0f, 3.0f);
    ROIState::Items().insert(ROIState::Items().begin(), unrelated);
    capture = ToolAssetService::ConfirmROICapture(tool, ToolAssetKind::ShapeTemplate);
    Require(capture.success && tool.shpTplImage.size() == cv::Size(25, 25),
        "stable runtime ROI capture failed after ROI index changed");

    capture = ToolAssetService::CaptureCurrentImage(tool, ToolAssetKind::DifferenceReference);
    Require(capture.success && tool.differenceReferenceImage.size() == image.size(),
        "difference reference capture did not use the current Core image");
    ToolAssetService::ClearAsset(tool, ToolAssetKind::DifferenceReference);
    Require(tool.differenceReferenceImage.empty(), "difference reference clear regressed");

    ToolAssetService::BeginROICapture(tool, ToolAssetKind::MultiColorReference);
    ToolAssetService::ForgetTool(tool.toolId);
    Require(!ToolAssetService::IsROICaptureActive(tool.toolId,
        ToolAssetKind::MultiColorReference), "deleted tool kept an asset capture session");

    ToolAssetService::ClearSessions();
    ROIState::ClearInteraction();
    ImageState::Clear();
}

void TestToolROIServiceOwnsBoundROIEditing()
{
    ToolROIService::ClearSessions();
    ROIState::ClearInteraction();

    ToolInstance tool;
    tool.toolId = 9101;
    int roiIndex = ToolROIService::BeginSearchROIEdit(tool);
    ROI* roi = ROIState::MutableAt(roiIndex);
    Require(roi != nullptr, "bound ROI edit did not create an editable ROI");
    roi->start = ImVec2(11.0f, 13.0f);
    roi->end = ImVec2(51.0f, 43.0f);

    ROI unrelated;
    unrelated.type = ROI_TYPE_RECT;
    unrelated.start = ImVec2(1.0f, 1.0f);
    unrelated.end = ImVec2(2.0f, 2.0f);
    ROIState::Items().insert(ROIState::Items().begin(), unrelated);

    const ToolROIEditResult result = ToolROIService::ConfirmSearchROIEdit(tool);
    Require(result.success && tool.searchROIs.size() == 1 &&
        tool.searchROIs[0].ToCvRect() == cv::Rect(11, 13, 40, 30),
        "bound ROI confirmation followed a stale vector index");
    Require(tool.useSearchROI && tool.yoloUseROI && tool.mcfUseROI &&
        tool.ocrUseROI && tool.qrUseROI && tool.mcfRoiW == 40,
        "bound ROI flags were not updated consistently");

    const ROI saved = tool.searchROIs[0];
    roiIndex = ToolROIService::BeginSearchROIEdit(tool);
    roi = ROIState::MutableAt(roiIndex);
    Require(roi != nullptr, "bound ROI modification did not reopen the saved ROI");
    roi->start = ImVec2(20.0f, 20.0f);
    ToolROIService::CancelSearchROIEdit(tool.toolId);
    Require(tool.searchROIs[0].ToCvRect() == saved.ToCvRect(),
        "cancelled bound ROI edit changed the saved tool ROI");

    ToolROIService::ClearSearchROIs(tool);
    Require(tool.searchROIs.empty() && tool.lineSaveROIs.empty() &&
        !tool.useSearchROI && !tool.yoloUseROI && tool.mcfRoiW == 0,
        "bound ROI clear did not reset all tool input flags");

    ROIState::ClearInteraction();
    ToolInstance measurement;
    ROI legacyMeasurementROI;
    legacyMeasurementROI.type = ROI_TYPE_LINE;
    legacyMeasurementROI.start = ImVec2(10.0f, 20.0f);
    legacyMeasurementROI.end = ImVec2(80.0f, 20.0f);
    measurement.searchROIs.push_back(legacyMeasurementROI);
    ROIState::Add(legacyMeasurementROI, false);
    Require(ToolROIService::SyncMeasurementROIs(measurement) &&
        measurement.measureRuntimeROIIds.size() == 1 &&
        measurement.measureRuntimeROIIds.front() != 0,
        "measurement ROI sync did not recover a legacy geometry binding");

    const int measurementIndex = ROIState::FindIndexByRuntimeId(
        measurement.measureRuntimeROIIds.front());
    ROI* runtimeMeasurementROI = ROIState::MutableAt(measurementIndex);
    Require(runtimeMeasurementROI != nullptr, "measurement runtime ROI was not recoverable by id");
    const std::vector<ROI> measurementBackup = measurement.searchROIs;
    runtimeMeasurementROI->end.x = 95.0f;
    Require(ToolROIService::SyncMeasurementROIs(measurement) &&
        std::abs(measurement.searchROIs.front().end.x - 95.0f) < 0.001f,
        "measurement ROI sync did not publish edited geometry");

    ToolROIService::RestoreMeasurementROIBackup(measurement, measurementBackup);
    Require(std::abs(ROIState::At(measurementIndex)->end.x - 80.0f) < 0.001f,
        "measurement ROI cancel did not restore the Core ROI backup");
    ToolROIService::RemoveMeasurementROIs(measurement);
    Require(measurement.searchROIs.empty() && measurement.measureRuntimeROIIds.empty() &&
        ROIState::ReadOnlyItems().empty(),
        "measurement ROI removal left tool or Core state behind");

    measurement.searchROIs.push_back(legacyMeasurementROI);
    ToolROIService::RestoreMeasurementROIs(measurement);
    Require(measurement.measureRuntimeROIIds.size() == 1 &&
        ROIState::FindIndexByRuntimeId(measurement.measureRuntimeROIIds.front()) >= 0 &&
        ToolROIService::SelectMeasurementROI(measurement),
        "measurement ROI restore did not create and select a runtime ROI");
    ToolROIService::RemoveMeasurementROIs(measurement);
    ToolROIService::ClearSessions();
    ROIState::ClearInteraction();
}

void TestHardwareAdapterServiceLifecycle()
{
    HardwareAdapterService::Clear();
    bool cameraDisconnected = false;
    auto camera = std::make_unique<TestCameraAdapter>(&cameraDisconnected);
    TestCameraAdapter* cameraView = camera.get();
    HardwareAdapterService::SetCamera(std::move(camera));
    Require(HardwareAdapterService::Camera() == cameraView,
        "camera adapter registration failed");
    Require(cameraView->Connect({"camera-1", 0, {}}).success,
        "camera adapter connect contract failed");
    Require(HardwareRuntimeService::GrabCameraFrame(100, "camera-1", 7, 42.0).success &&
        FrameSourceState::HasFrame() && FrameSourceState::Current().sourceType == FrameSourceType::Camera &&
        FrameSourceState::Current().frameIndex == 7 && ImageState::Current().size() == cv::Size(6, 4),
        "camera runtime service did not publish the frame source");

    bool plcDisconnected = false;
    Require(HardwareAdapterService::Register("plc-main",
        std::make_unique<TestPlcAdapter>(&plcDisconnected)),
        "PLC adapter registration failed");
    Require(!HardwareAdapterService::Register("plc-main",
        std::make_unique<TestPlcAdapter>(nullptr)),
        "duplicate hardware adapter key was accepted");
    IDeviceAdapter* base = HardwareAdapterService::Find("plc-main");
    Require(base && base->Connect({"127.0.0.1", 502, {}}).success,
        "registered PLC adapter lookup/connect failed");
    auto* plc = dynamic_cast<IPlcAdapter*>(base);
    auto* plcView = dynamic_cast<TestPlcAdapter*>(base);
    DeviceValue value;
    Require(plc && plc->ReadTag("ready", value).success &&
        std::get<bool>(value), "PLC tag read contract failed");
    HardwareOutputBinding plcOutput;
    plcOutput.kind = HardwareOutputKind::PlcTag;
    plcOutput.adapterKey = "plc-main";
    plcOutput.target = "inspection.ok";
    Require(HardwareRuntimeService::PublishInspectionStatus(
        ToolResultStatus::Pass, plcOutput).success && plcView &&
        plcView->lastWriteTag == "inspection.ok" && std::get<bool>(plcView->lastWriteValue),
        "inspection status was not published to PLC");

    auto modbus = std::make_unique<TestModbusAdapter>();
    TestModbusAdapter* modbusView = modbus.get();
    Require(HardwareAdapterService::Register("modbus-main", std::move(modbus)) &&
        modbusView->Connect({"127.0.0.1", 502, {}}).success,
        "Modbus adapter registration failed");
    HardwareOutputBinding modbusOutput;
    modbusOutput.kind = HardwareOutputKind::ModbusCoil;
    modbusOutput.adapterKey = "modbus-main";
    modbusOutput.address = 17;
    Require(HardwareRuntimeService::PublishInspectionStatus(
        ToolResultStatus::Fail, modbusOutput).success &&
        modbusView->lastAddress == 17 && !modbusView->lastValue,
        "inspection status was not published to Modbus coil");

    auto opcUa = std::make_unique<TestOpcUaAdapter>();
    TestOpcUaAdapter* opcUaView = opcUa.get();
    Require(HardwareAdapterService::Register("opcua-main", std::move(opcUa)) &&
        opcUaView->Connect({"opc.tcp://127.0.0.1", 4840, {}}).success,
        "OPC UA adapter registration failed");
    HardwareOutputBinding opcUaOutput;
    opcUaOutput.kind = HardwareOutputKind::OpcUaNode;
    opcUaOutput.adapterKey = "opcua-main";
    opcUaOutput.target = "ns=2;s=Inspection.OK";
    opcUaOutput.invert = true;
    Require(HardwareRuntimeService::PublishInspectionStatus(
        ToolResultStatus::Fail, opcUaOutput).success &&
        opcUaView->lastNodeId == opcUaOutput.target && std::get<bool>(opcUaView->lastValue),
        "inspection status was not published to OPC UA node");

    Require(HardwareAdapterService::Keys() ==
        std::vector<std::string>{"modbus-main", "opcua-main", "plc-main"},
        "hardware adapter key enumeration regressed");

    HardwareAdapterService::DisconnectAll();
    Require(cameraDisconnected && cameraView->stopped && plcDisconnected,
        "hardware adapters were not disconnected as a group");
    HardwareAdapterService::Clear();
    Require(HardwareAdapterService::Camera() == nullptr &&
        HardwareAdapterService::Keys().empty(),
        "hardware adapter service clear regressed");
    FrameSourceState::Clear();
}

void TestHardwareRuntimeAutomation()
{
    HardwareRuntimeService::Shutdown();
    ImageState::Clear();
    FrameSourceState::Clear();

    auto camera = std::make_unique<TestCameraAdapter>(nullptr);
    TestCameraAdapter* cameraView = camera.get();
    HardwareAdapterService::SetCamera(std::move(camera));
    Require(cameraView->Connect({"camera-auto", 0, {}}).success,
        "automation camera connect failed");

    HardwareCameraConnectionConfig captureConfig;
    captureConfig.sourceName = "camera-auto";
    captureConfig.autoCapture = false;
    captureConfig.grabTimeoutMs = 100;
    captureConfig.captureIntervalMs = 10;
    Require(HardwareRuntimeService::StartCameraCapture(captureConfig).success,
        "registered camera capture worker failed to start");
    HardwareRuntimeService::RequestCameraFrame();
    for (int attempt = 0; attempt < 200; ++attempt)
    {
        HardwareRuntimeService::Tick();
        if (HardwareRuntimeService::Snapshot().cameraFrameIndex > 0)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const HardwareRuntimeSnapshot cameraSnapshot = HardwareRuntimeService::Snapshot();
    Require(cameraSnapshot.cameraFrameIndex >= 1 &&
        cameraSnapshot.lastCameraOperation.success &&
        FrameSourceState::Current().sourceType == FrameSourceType::Camera &&
        ImageState::Current().size() == cv::Size(6, 4) &&
        ImageState::NeedUploadRef() && !ImageState::PendingUploadRef().empty(),
        "asynchronous industrial-camera frame was not published on Tick");

    ToolController::Reset();
    ToolChainState::ClearTools();
    ToolInstance cameraInputTool;
    cameraInputTool.type = 12;
    ToolChainState::AddTool(std::move(cameraInputTool));
    const int firstFrameIndex = cameraSnapshot.cameraFrameIndex;
    HardwareRuntimeService::RequestCameraFrame(true);
    bool linkedRunStarted = false;
    for (int attempt = 0; attempt < 200; ++attempt)
    {
        HardwareRuntimeService::Tick();
        if (HardwareRuntimeService::Snapshot().cameraFrameIndex > firstFrameIndex &&
            ToolController::GetMode() == ToolController::Mode::Running)
        {
            linkedRunStarted = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Require(linkedRunStarted,
        "camera capture did not trigger the tool chain after publishing a new frame");
    ToolController::Reset();
    ToolChainState::ClearTools();

    auto modbus = std::make_unique<TestModbusAdapter>();
    TestModbusAdapter* modbusView = modbus.get();
    Require(HardwareAdapterService::Register("automation-output", std::move(modbus)) &&
        modbusView->Connect({"127.0.0.1", 502, {}}).success,
        "automation output adapter registration failed");
    HardwareOutputBinding output;
    output.kind = HardwareOutputKind::ModbusCoil;
    output.adapterKey = "automation-output";
    output.address = 23;
    HardwareRuntimeService::ConfigureOutputBinding(output, true);

    ToolResult pass;
    pass.status = ToolResultStatus::Pass;
    ToolResult skippedFailure;
    skippedFailure.status = ToolResultStatus::Fail;
    skippedFailure.skipped = true;
    Require(HardwareRuntimeService::AggregateInspectionStatus({pass, skippedFailure}) ==
        ToolResultStatus::Pass &&
        HardwareRuntimeService::PublishInspectionResults({pass, skippedFailure}).success &&
        modbusView->lastAddress == 23 && modbusView->lastValue,
        "skipped result incorrectly changed automatic inspection output");

    ToolResult fail;
    fail.status = ToolResultStatus::Fail;
    Require(HardwareRuntimeService::PublishInspectionResults({pass, fail}).success &&
        !modbusView->lastValue,
        "failed tool result was not published as NG");

    ToolResult error;
    error.status = ToolResultStatus::Error;
    Require(HardwareRuntimeService::AggregateInspectionStatus({fail, error}) ==
        ToolResultStatus::Error &&
        HardwareRuntimeService::AggregateInspectionStatus({skippedFailure}) ==
        ToolResultStatus::Error,
        "inspection status aggregation priority regressed");

    auto tcpTransport = std::make_unique<ScriptedTcpTextTransport>();
    ScriptedTcpTextTransport* tcpTransportView = tcpTransport.get();
    auto tcpText = std::make_unique<TcpTextAdapter>(std::move(tcpTransport));
    Require(tcpText->Connect({"192.168.10.5", 5000, {}, 1500}).success &&
        HardwareAdapterService::Register("tcp-text-output", std::move(tcpText)),
        "TCP text automation output setup failed");
    HardwareOutputBinding tcpBinding;
    tcpBinding.kind = HardwareOutputKind::TcpText;
    tcpBinding.adapterKey = "tcp-text-output";
    tcpBinding.passText = "OK";
    tcpBinding.failText = "NG";
    tcpBinding.appendCrLf = true;
    HardwareRuntimeService::ConfigureOutputBinding(tcpBinding, true);
    Require(HardwareRuntimeService::PublishConfiguredStatus(ToolResultStatus::Pass).success &&
        tcpTransportView->lastText == "OK\r\n" && tcpTransportView->sendCount == 1,
        "TCP text Pass output waited for a response or changed the payload");
    Require(HardwareRuntimeService::PublishConfiguredStatus(ToolResultStatus::Error).success &&
        tcpTransportView->lastText == "NG\r\n" && tcpTransportView->sendCount == 2,
        "TCP text Error output did not use the configured Fail payload");

    HardwareRuntimeService::Shutdown();
    Require(HardwareAdapterService::Camera() == nullptr &&
        HardwareAdapterService::Keys().empty(),
        "hardware runtime shutdown left registered devices behind");
    FrameSourceState::Clear();
    ImageState::Clear();
}

void TestConcreteTcpTextAdapter()
{
    auto transport = std::make_unique<ScriptedTcpTextTransport>();
    ScriptedTcpTextTransport* transportView = transport.get();
    TcpTextAdapter adapter(std::move(transport));

    DeviceEndpoint endpoint;
    endpoint.address = "192.168.10.5";
    endpoint.port = 5000;
    endpoint.timeoutMs = 1500;
    Require(adapter.Connect(endpoint).success &&
        adapter.ConnectionState() == DeviceConnectionState::Connected &&
        transportView->lastAddress == endpoint.address &&
        transportView->lastPort == endpoint.port &&
        transportView->lastTimeoutMs == endpoint.timeoutMs,
        "TCP text adapter did not apply endpoint settings");
    Require(adapter.SendText("PASS\r\n").success &&
        transportView->lastText == "PASS\r\n" && transportView->sendCount == 1,
        "TCP text adapter did not send the exact configured payload");

    transportView->failNextSend = true;
    Require(!adapter.SendText("FAIL\r\n").success &&
        adapter.ConnectionState() == DeviceConnectionState::Fault,
        "TCP text send failure did not fault the connection");
    adapter.Disconnect();
    Require(adapter.ConnectionState() == DeviceConnectionState::Disconnected,
        "TCP text adapter disconnect state regressed");
}

void TestConcreteModbusTcpAdapterProtocol()
{
    auto transport = std::make_unique<ScriptedModbusTransport>();
    ScriptedModbusTransport* transportView = transport.get();
    ModbusTcpAdapter adapter(std::move(transport));

    DeviceEndpoint endpoint;
    endpoint.address = "plc.example";
    endpoint.port = 1502;
    endpoint.resource = "7";
    endpoint.timeoutMs = 750;
    Require(adapter.Connect(endpoint).success &&
        adapter.ConnectionState() == DeviceConnectionState::Connected &&
        transportView->lastAddress == endpoint.address &&
        transportView->lastPort == endpoint.port &&
        transportView->lastTimeoutMs == endpoint.timeoutMs,
        "concrete Modbus TCP adapter did not apply endpoint settings");

    adapter.Disconnect();
    endpoint.resource = "not-a-unit-id";
    const DeviceOperationResult invalidUnit = adapter.Connect(endpoint);
    Require(!invalidUnit.success &&
        invalidUnit.message.find("Unit ID") != std::string::npos &&
        adapter.ConnectionState() == DeviceConnectionState::Disconnected,
        "invalid Modbus Unit ID silently fell back to a different device");
    endpoint.resource = "7";
    Require(adapter.Connect(endpoint).success,
        "Modbus adapter did not reconnect after Unit ID validation failure");

    std::vector<bool> coils;
    Require(adapter.ReadCoils(0x0013, 3, coils).success &&
        coils == std::vector<bool>({true, false, true}) &&
        transportView->lastRequest.size() == 12 &&
        transportView->lastRequest[6] == 7 &&
        transportView->lastRequest[7] == 1 &&
        transportView->lastRequest[8] == 0 &&
        transportView->lastRequest[9] == 0x13,
        "Modbus function 01 request or bit decoding regressed");

    Require(adapter.WriteCoil(17, true).success &&
        transportView->lastRequest[7] == 5 &&
        transportView->lastRequest[10] == 0xff &&
        transportView->lastRequest[11] == 0,
        "Modbus function 05 request/echo validation regressed");

    std::vector<std::uint16_t> registers;
    Require(adapter.ReadHoldingRegisters(4, 2, registers).success &&
        registers == std::vector<std::uint16_t>({0x1234, 0xabcd}) &&
        transportView->lastRequest[7] == 3,
        "Modbus function 03 register decoding regressed");
    Require(adapter.WriteHoldingRegister(9, 0x4567).success &&
        transportView->lastRequest[7] == 6 &&
        transportView->lastRequest[10] == 0x45 &&
        transportView->lastRequest[11] == 0x67,
        "Modbus function 06 request/echo validation regressed");

    Require(!adapter.ReadCoils(0, 0, coils).success &&
        adapter.ConnectionState() == DeviceConnectionState::Connected,
        "invalid Modbus count should fail without dropping the connection");
    transportView->exceptionNext = true;
    const DeviceOperationResult exception = adapter.ReadCoils(0, 1, coils);
    Require(!exception.success && exception.message.find("illegal data address") != std::string::npos &&
        adapter.ConnectionState() == DeviceConnectionState::Connected,
        "Modbus exception response was not decoded without faulting the transport");

    transportView->failNextExchange = true;
    const DeviceOperationResult transportFailure = adapter.ReadHoldingRegisters(0, 1, registers);
    Require(!transportFailure.success &&
        transportFailure.message.find("FC=03") != std::string::npos &&
        transportFailure.message.find("UnitId=7") != std::string::npos &&
        transportFailure.message.find("地址=0") != std::string::npos &&
        adapter.ConnectionState() == DeviceConnectionState::Fault,
        "Modbus transport failure did not include request context or fault the adapter");
    adapter.Disconnect();
    Require(adapter.ConnectionState() == DeviceConnectionState::Disconnected,
        "Modbus adapter disconnect state regressed");
}

void TestConcreteOpenCvCameraAdapter()
{
    HardwareAdapterService::Clear();
    ImageState::Clear();
    FrameSourceState::Clear();

    int backendCloseCount = 0;
    auto backend = std::make_unique<ScriptedCameraBackend>();
    backend->externalCloseCount = &backendCloseCount;
    ScriptedCameraBackend* backendView = backend.get();
    auto camera = std::make_unique<OpenCvCameraAdapter>(std::move(backend));
    OpenCvCameraAdapter* cameraView = camera.get();
    HardwareAdapterService::SetCamera(std::move(camera));

    cv::Mat frame;
    Require(!cameraView->GrabFrame(frame, 10).success,
        "OpenCV camera adapter captured before connection");

    DeviceEndpoint endpoint;
    endpoint.address = "0";
    endpoint.resource = "dshow";
    endpoint.timeoutMs = 640;
    Require(cameraView->Connect(endpoint).success &&
        cameraView->ConnectionState() == DeviceConnectionState::Connected &&
        backendView->endpoint.address == "0" &&
        backendView->endpoint.resource == "dshow" &&
        backendView->endpoint.timeoutMs == 640,
        "OpenCV camera adapter did not apply the capture endpoint");
    Require(cameraView->StartStream().success && cameraView->IsStreaming(),
        "OpenCV camera adapter stream state did not start");

    Require(HardwareRuntimeService::GrabCameraFrame(250, "opencv-camera", 9, 88.0).success &&
        backendView->lastTimeoutMs == 250 &&
        FrameSourceState::HasFrame() &&
        FrameSourceState::Current().sourcePath == "opencv-camera" &&
        FrameSourceState::Current().frameIndex == 9 &&
        ImageState::Current().size() == cv::Size(7, 5),
        "OpenCV camera frame did not enter the normal FrameSource pipeline");

    backendView->nextFrame.release();
    Require(!HardwareRuntimeService::GrabCameraFrame(100).success &&
        cameraView->ConnectionState() == DeviceConnectionState::Connected,
        "empty camera frame should fail without dropping the connection");
    cameraView->StopStream();
    Require(!cameraView->IsStreaming(), "OpenCV camera adapter stream state did not stop");
    HardwareAdapterService::Clear();
    Require(backendCloseCount >= 1,
        "OpenCV camera backend was not closed during adapter cleanup");

    auto failingBackend = std::make_unique<ScriptedCameraBackend>();
    failingBackend->openSucceeds = false;
    OpenCvCameraAdapter failingCamera(std::move(failingBackend));
    Require(!failingCamera.Connect(endpoint).success &&
        failingCamera.ConnectionState() == DeviceConnectionState::Fault &&
        !failingCamera.LastError().empty(),
        "OpenCV camera connection failure did not publish fault state");
    ImageState::Clear();
    FrameSourceState::Clear();
}

void TestConcreteOpen62541OpcUaAdapter()
{
    HardwareAdapterService::Clear();
    LocalOpcUaTestServer server;
    Require(server.Start(), "local OPC UA test server failed to start");

    auto adapter = std::make_unique<Open62541OpcUaAdapter>();
    Open62541OpcUaAdapter* adapterView = adapter.get();
    DeviceEndpoint endpoint;
    endpoint.address = "127.0.0.1";
    endpoint.port = server.Port();
    endpoint.timeoutMs = 1000;
    Require(adapterView->Connect(endpoint).success &&
        adapterView->ConnectionState() == DeviceConnectionState::Connected &&
        std::string(adapterView->AdapterName()).find("open62541") != std::string::npos,
        "native open62541 OPC UA adapter failed to connect");

    DeviceValue value;
    Require(adapterView->ReadNode("ns=1;s=InspectionPass", value).success &&
        !std::get<bool>(value), "OPC UA Boolean read failed");
    Require(adapterView->WriteNode("ns=1;s=InspectionPass", DeviceValue(true)).success &&
        adapterView->ReadNode("ns=1;s=InspectionPass", value).success &&
        std::get<bool>(value), "OPC UA Boolean write failed");

    Require(adapterView->ReadNode("ns=1;s=Count", value).success &&
        std::get<std::int64_t>(value) == 7,
        "OPC UA Int32 read conversion failed");
    Require(adapterView->WriteNode("ns=1;s=Count", DeviceValue(std::int64_t(42))).success &&
        adapterView->ReadNode("ns=1;s=Count", value).success &&
        std::get<std::int64_t>(value) == 42,
        "OPC UA integer write did not preserve the target node type");
    Require(!adapterView->WriteNode("ns=1;s=Count",
        DeviceValue(std::int64_t(1) << 40)).success &&
        adapterView->ConnectionState() == DeviceConnectionState::Connected,
        "OPC UA integer range validation regressed");

    Require(adapterView->ReadNode("ns=1;s=Ratio", value).success &&
        std::abs(std::get<double>(value) - 1.25) < 0.001,
        "OPC UA Float read conversion failed");
    Require(adapterView->WriteNode("ns=1;s=Ratio", DeviceValue(2.75)).success &&
        adapterView->ReadNode("ns=1;s=Ratio", value).success &&
        std::abs(std::get<double>(value) - 2.75) < 0.001,
        "OPC UA floating-point write did not preserve the target node type");

    Require(adapterView->ReadNode("ns=1;s=LineName", value).success &&
        std::get<std::string>(value) == "line-a",
        "OPC UA String read failed");
    Require(adapterView->WriteNode("ns=1;s=LineName",
        DeviceValue(std::string("line-b"))).success &&
        adapterView->ReadNode("ns=1;s=LineName", value).success &&
        std::get<std::string>(value) == "line-b",
        "OPC UA String write failed");

    Require(!adapterView->ReadNode("not-a-node-id", value).success &&
        adapterView->ConnectionState() == DeviceConnectionState::Connected,
        "invalid OPC UA NodeId should not drop the connection");

    Require(HardwareAdapterService::Register("opcua-native", std::move(adapter)),
        "native OPC UA adapter registration failed");
    HardwareOutputBinding output;
    output.kind = HardwareOutputKind::OpcUaNode;
    output.adapterKey = "opcua-native";
    output.target = "ns=1;s=InspectionPass";
    Require(HardwareRuntimeService::PublishInspectionStatus(
        ToolResultStatus::Fail, output).success &&
        adapterView->ReadNode(output.target, value).success && !std::get<bool>(value),
        "inspection status did not flow through the native OPC UA adapter");
    Require(HardwareRuntimeService::PublishInspectionStatus(
        ToolResultStatus::Pass, output).success &&
        adapterView->ReadNode(output.target, value).success && std::get<bool>(value),
        "passing inspection status did not reach the OPC UA node");

    HardwareAdapterService::Clear();
    Require(HardwareAdapterService::Find("opcua-native") == nullptr,
        "native OPC UA adapter cleanup regressed");
}

void TestModbusPlcTagMappingAdapter()
{
    HardwareAdapterService::Clear();
    auto modbus = std::make_unique<TestModbusAdapter>();
    TestModbusAdapter* modbusView = modbus.get();
    auto plc = std::make_unique<ModbusPlcAdapter>(std::move(modbus));
    ModbusPlcAdapter* plcView = plc.get();

    ModbusPlcTagBinding ready;
    ready.kind = ModbusPlcTagKind::Coil;
    ready.valueType = ModbusPlcValueType::Boolean;
    ready.address = 2;
    Require(plcView->ConfigureTag("ready", ready),
        "Modbus PLC boolean tag configuration failed");

    ModbusPlcTagBinding count;
    count.kind = ModbusPlcTagKind::HoldingRegister;
    count.valueType = ModbusPlcValueType::UInt16;
    count.address = 10;
    Require(plcView->ConfigureTag("count", count),
        "Modbus PLC UInt16 tag configuration failed");

    ModbusPlcTagBinding temperature;
    temperature.kind = ModbusPlcTagKind::HoldingRegister;
    temperature.valueType = ModbusPlcValueType::ScaledDouble;
    temperature.address = 11;
    temperature.scale = 0.1;
    temperature.offset = -20.0;
    Require(plcView->ConfigureTag("temperature", temperature),
        "Modbus PLC scaled tag configuration failed");

    ModbusPlcTagBinding invalidScaled = temperature;
    invalidScaled.scale = 0.0;
    Require(!plcView->ConfigureTag("invalid-scale", invalidScaled),
        "Modbus PLC accepted a zero-scale tag");
    ModbusPlcTagBinding invalidCoil = ready;
    invalidCoil.valueType = ModbusPlcValueType::UInt16;
    Require(!plcView->ConfigureTag("invalid-coil", invalidCoil),
        "Modbus PLC accepted a non-boolean coil tag");

    Require(plcView->Connect({"127.0.0.1", 502, "3"}).success,
        "Modbus PLC adapter connect delegation failed");
    DeviceValue value;
    modbusView->nextCoilValue = true;
    Require(plcView->ReadTag("ready", value).success && std::get<bool>(value),
        "Modbus PLC coil tag read failed");
    modbusView->nextRegisterValue = 123;
    Require(plcView->ReadTag("count", value).success &&
        std::get<std::int64_t>(value) == 123,
        "Modbus PLC UInt16 tag read failed");
    modbusView->nextRegisterValue = 250;
    Require(plcView->ReadTag("temperature", value).success &&
        std::abs(std::get<double>(value) - 5.0) < 0.001,
        "Modbus PLC engineering-unit conversion failed");

    Require(plcView->WriteTag("ready", DeviceValue(false)).success &&
        modbusView->lastAddress == 2 && !modbusView->lastValue,
        "Modbus PLC coil tag write failed");
    Require(plcView->WriteTag("count", DeviceValue(std::int64_t(42))).success &&
        modbusView->lastRegisterAddress == 10 && modbusView->lastRegisterValue == 42,
        "Modbus PLC UInt16 tag write failed");
    Require(plcView->WriteTag("temperature", DeviceValue(7.5)).success &&
        modbusView->lastRegisterAddress == 11 && modbusView->lastRegisterValue == 275,
        "Modbus PLC scaled tag write failed");
    Require(!plcView->ReadTag("missing", value).success &&
        !plcView->WriteTag("count", DeviceValue(std::int64_t(70000))).success,
        "Modbus PLC accepted an unknown tag or out-of-range register value");

    Require(HardwareAdapterService::Register("mapped-plc", std::move(plc)),
        "Modbus PLC adapter registration failed");
    HardwareOutputBinding output;
    output.kind = HardwareOutputKind::PlcTag;
    output.adapterKey = "mapped-plc";
    output.target = "ready";
    Require(HardwareRuntimeService::PublishInspectionStatus(
        ToolResultStatus::Pass, output).success && modbusView->lastValue,
        "inspection status did not flow through the mapped PLC tag");
    HardwareAdapterService::Clear();
}

void TestCalibrationFitter()
{
    const std::vector<CalibrationSample> samples = {
        {{0.0, 0.0}, {10.0, 20.0}},
        {{100.0, 0.0}, {30.0, 20.0}},
        {{0.0, 50.0}, {10.0, 35.0}},
        {{100.0, 50.0}, {30.0, 35.0}}
    };
    const CalibrationFitResult scale = CalibrationFitter::FitScale(samples);
    Require(scale.success && std::abs(scale.model.scaleX - 0.2) < 1.0e-6 &&
        std::abs(scale.model.scaleY - 0.3) < 1.0e-6 && scale.rmsError < 1.0e-6,
        "multi-point scale calibration fit regressed");

    const CalibrationFitResult homography = CalibrationFitter::FitHomography(samples);
    Require(homography.success && homography.model.homographyEnabled && homography.rmsError < 1.0e-4,
        "homography calibration fit regressed");

    const auto path = std::filesystem::temp_directory_path() / "imgui_opencv_calibration.json";
    Require(CalibrationFitter::Save(path.string().c_str(), scale.model),
        "calibration model save failed");
    CalibrationModel loaded;
    Require(CalibrationFitter::Load(path.string().c_str(), loaded) &&
        std::abs(loaded.scaleX - scale.model.scaleX) < 1.0e-6,
        "calibration model load failed");
    std::vector<CalibrationSample> loadedSamples;
    Require(CalibrationFitter::LoadDocument(path.string().c_str(), loaded, loadedSamples) &&
        loadedSamples.empty(), "legacy model-only calibration document compatibility regressed");

    Require(CalibrationFitter::SaveDocument(path.string().c_str(),
        homography.model, samples), "calibration document save failed");
    loaded = CalibrationModel{};
    loadedSamples.clear();
    Require(CalibrationFitter::LoadDocument(path.string().c_str(), loaded, loadedSamples) &&
        loaded.homographyEnabled && loadedSamples.size() == samples.size(),
        "calibration document load failed");
    const CalibrationFitResult evaluation = CalibrationFitter::Evaluate(loaded, loadedSamples);
    Require(evaluation.success && evaluation.residuals.size() == samples.size() &&
        evaluation.rmsError < 1.0e-4, "calibration document residual evaluation regressed");
    std::filesystem::remove(path);
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
    ImageState::Clear();
    gContext.Clear();

    cv::Mat frame(18, 24, CV_8UC3, cv::Scalar(12, 34, 56));
    FrameSourceState::SetCurrentFrame(frame, FrameSourceType::ImageSequence, "seq/a.png", 3, 120.0);

    Require(!ImageState::Current().empty(), "frame source did not update current image");
    Require(!ImageState::Original().empty(), "frame source did not update original image");
    Require(!gContext.image.empty(), "frame source did not update VisionContext image");
    Require(!gContext.originalImage.empty(), "frame source did not update VisionContext original image");
    Require(ImageState::Current().cols == 24 && ImageState::Current().rows == 18,
        "frame source dimensions regressed");
    Require(ImageState::Width() == 24 && ImageState::Height() == 18,
        "frame source image state size regressed");
    Require(gContext.width == 24 && gContext.height == 18, "frame source context size regressed");
    Require(ImageState::Version() == 1 && gContext.imageVersion == 1, "frame source version regressed");

    const FramePacket& packet = FrameSourceState::Current();
    Require(packet.sourceType == FrameSourceType::ImageSequence, "frame source type regressed");
    Require(packet.sourcePath == "seq/a.png", "frame source path regressed");
    Require(packet.frameIndex == 3, "frame source index regressed");
    Require(std::abs(packet.timestampMs - 120.0) < 0.001, "frame source timestamp regressed");

    frame.setTo(cv::Scalar(200, 200, 200));
    Require(ImageState::Original().at<cv::Vec3b>(0, 0)[0] == 12,
        "frame source kept shallow original copy");

    FrameSourceState::Clear();
    Require(!FrameSourceState::HasFrame(), "frame source clear left stale current frame");
    Require(!gContext.frame.valid(), "frame source clear left stale context frame");
}

void TestImageStateOwnsCurrentImageSnapshot()
{
    gContext.Clear();
    ImageState::Clear();

    cv::Mat image(10, 14, CV_8UC3, cv::Scalar(7, 8, 9));
    ImageState::SetImage(image);

    Require(ImageState::HasImage(), "image state did not accept image");
    Require(ImageState::Width() == 14 && ImageState::Height() == 10, "image state dimensions regressed");
    Require(ImageState::Version() == 1, "image state version did not increment");
    Require(!ImageState::Current().empty(), "image state current image empty");
    Require(!ImageState::Original().empty(), "image state original image empty");
    Require(gContext.imageVersion == 1, "image state did not sync context version");
    Require(gContext.image.data != ImageState::Current().data, "image state shared VisionContext current image buffer");

    ImageState::CurrentRef().setTo(cv::Scalar(11, 12, 13));
    Require(ImageState::Current().at<cv::Vec3b>(0, 0)[0] == 11,
        "image state mutable API did not update current image");

    image.setTo(cv::Scalar(200, 200, 200));
    Require(ImageState::Current().at<cv::Vec3b>(0, 0)[0] == 11, "image state current buffer was unexpectedly replaced");
    Require(ImageState::Original().at<cv::Vec3b>(0, 0)[0] == 7,
        "image state kept shallow original copy");

    ImageState::Clear();
    Require(!ImageState::HasImage(), "image state clear left current image");
    Require(ImageState::Current().empty() && ImageState::Original().empty(),
        "image state clear did not clear images");
    Require(ImageState::Width() == 0 && ImageState::Height() == 0,
        "image state clear did not reset dimensions");
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
    cv::Mat input = cv::Mat::zeros(32, 32, CV_8UC1);
    cv::rectangle(input, cv::Rect(8, 8, 8, 8), cv::Scalar(255), cv::FILLED);
    ImageState::SetImage(input);
    gContext.Clear();

    ToolInstance it;
    it.type = 2;
    it.blobMinArea = 20;
    it.blobMaxArea = 200;

    ToolExecutor::Execute(it.type, it);
    Require(gContext.image.data == ImageState::Current().data,
        "tool executor did not share read-only blob input");
    Require(!gContext.unifiedResults.empty(), "tool executor did not publish result");
    Require(gContext.unifiedResults[0].regions.size() == 1, "tool executor blob result regressed");

    ToolInstance threshold;
    threshold.type = 3;
    threshold.dbgUseGray = false;
    threshold.dbgEnableThresh = true;
    threshold.dbgThreshold = 100;
    ToolExecutor::Execute(threshold.type, threshold);
    Require(gContext.image.data != ImageState::Current().data,
        "tool executor shared mutable threshold input");
}

void TestToolChainEditActions()
{
    ToolChainState::ClearTools();
    ToolChainState::Tools().resize(3);
    auto& tools = ToolChainState::Tools();
    tools[0].type = 12;
    tools[1].type = 5;
    tools[2].type = 7;
    tools[2].resultRoiSourceTool = 1;
    tools[2].resultRoiSourceToolId = 5001;
    tools[2].fixture.sourceToolIndex = 1;
    tools[2].fixture.sourceToolId = 5001;
    ToolChainState::SetActiveIndex(2);
    ToolChainState::SetYoloLiveInstanceIndex(2);
    ToolChainState::SetYoloLiveDetect(true);

    Require(ToolChainState::MoveTool(2, 1),
        "tool chain move up was rejected");
    Require(tools[1].type == 7 && tools[2].type == 5, "tool chain move order regressed");
    Require(tools[1].resultRoiSourceTool == 2 && tools[1].fixture.sourceToolIndex == 2,
        "tool chain move did not remap dependencies");
    Require(ToolChainState::ActiveIndex() == 1 &&
        ToolChainState::YoloLiveInstanceIndex() == 1 &&
        ToolChainState::YoloLiveDetect(),
        "tool chain move did not preserve active/live index");

    Require(!ToolChainState::MoveTool(1, 0),
        "tool chain allowed moving ahead of original tool");
    Require(ToolChainState::RemoveTool(0),
        "tool chain rejected deleting original tool");
    Require(tools.size() == 2 && tools[0].type == 7 && tools[1].type == 5,
        "tool chain original deletion order regressed");
    Require(tools[0].resultRoiSourceTool == 1 && tools[0].fixture.sourceToolIndex == 1,
        "tool chain original deletion did not remap dependencies");
    Require(ToolChainState::ActiveIndex() == 0 &&
        ToolChainState::YoloLiveInstanceIndex() == 0 &&
        ToolChainState::YoloLiveDetect(),
        "tool chain original deletion did not remap selected/live index");

    bool destroyed = false;
    tools[0].toolImpl = new TestDisposableTool(&destroyed);
    Require(ToolChainState::RemoveTool(0),
        "tool chain remove was rejected");
    Require(destroyed, "tool chain remove did not release tool implementation");
    Require(tools.size() == 1 && tools[0].type == 5,
        "tool chain remove order regressed");
    Require(ToolChainState::ActiveIndex() == -1 &&
        ToolChainState::YoloLiveInstanceIndex() == -1 &&
        !ToolChainState::YoloLiveDetect(),
        "tool chain remove did not clear selected live tool");
    ToolChainState::ClearTools();
}

void TestImageViewStateOwnsTransform()
{
    ImageViewState::Zoom() = 3.5f;
    ImageViewState::Pan() = {12.0f, -8.0f};
    ImageViewState::CanvasSize() = {640.0f, 480.0f};
    ImageViewState::ImageScreenPos() = {20.0f, 30.0f};
    ImageViewState::ShowPixelGrid() = true;
    ImageViewState::ShowCoordGrid() = true;
    ImageViewState::GridStep() = 25;

    Require(std::abs(ImageViewState::Zoom() - 3.5f) < 0.0001f &&
        ImageViewState::Pan().x == 12.0f && ImageViewState::Pan().y == -8.0f,
        "image view state did not retain transform");

    ImageViewState::Reset();
    Require(std::abs(ImageViewState::Zoom() - 1.0f) < 0.0001f &&
        ImageViewState::Pan().x == 0.0f && ImageViewState::Pan().y == 0.0f &&
        !ImageViewState::ShowPixelGrid() && !ImageViewState::ShowCoordGrid() &&
        ImageViewState::GridStep() == 1,
        "image view state reset regressed");
}

void TestToolChainValidatorAndRunGuard()
{
    std::vector<ToolInstance> tools(2);
    tools[0].toolId = 100;
    tools[1].toolId = 200;

    tools[1].resultRoiSourceTool = 0;
    tools[1].resultRoiSourceToolId = tools[0].toolId;
    Require(ToolChainValidator::Validate(tools).valid(),
        "valid stable-id dependency was rejected");
    const std::vector<ToolChainDependency> dependencies =
        ToolChainValidator::DescribeDependencies(tools);
    Require(dependencies.size() == 1 && dependencies[0].valid &&
        dependencies[0].kind == ToolDependencyKind::ResultROI &&
        dependencies[0].consumerIndex == 1 && dependencies[0].sourceIndex == 0,
        "dependency description did not match validation resolution");

    tools[0].resultRoiSourceTool = 1;
    tools[0].resultRoiSourceToolId = tools[1].toolId;
    const ToolChainValidationResult future = ToolChainValidator::Validate(tools);
    Require(!future.valid(), "future dependency was not rejected");

    bool foundCycle = false;
    for (const ToolChainValidationIssue& issue : future.issues)
    {
        if (issue.message.find("循环依赖") != std::string::npos)
        {
            foundCycle = true;
            break;
        }
    }
    Require(foundCycle, "cyclic dependency was not reported");

    tools[0].resultRoiSourceTool = 0;
    tools[0].resultRoiSourceToolId = tools[0].toolId;
    Require(!ToolChainValidator::Validate(tools).valid(),
        "self dependency was not rejected");

    ToolController::OnToolChainChanged();
    ToolChainState::ClearTools();
    ToolChainState::Tools() = tools;
    ToolController::RequestRunAll(false);
    Require(ToolController::GetMode() == ToolController::Mode::Idle,
        "invalid tool chain was allowed to enter the run queue");
    ToolChainState::ClearTools();
    ToolController::OnToolChainChanged();
}

void TestToolChainPreflight()
{
    ToolInstance templateTool;
    templateTool.type = 1;
    std::vector<ToolInstance> tools{templateTool};

    ToolChainPreflightResult missing = ToolChainPreflight::Check(tools, false, 0);
    Require(!missing.valid(), "preflight accepted a missing image and template");

    tools[0].templateImg = cv::Mat::zeros(4, 4, CV_8UC1);
    Require(ToolChainPreflight::Check(tools, true, 0).valid(),
        "preflight rejected a complete template tool");

    tools[0].type = 4;
    tools[0].yoloModelPath = "missing-model.onnx";
    const ToolChainPreflightResult missingModel = ToolChainPreflight::Check(tools, true, 0);
    Require(!missingModel.valid(), "preflight accepted a missing YOLO model");
    tools[0].skipIfModelMissing = true;
    Require(ToolChainPreflight::Check(tools, true, 0).valid(),
        "preflight did not honor the missing-model skip policy");
}

void TestToolChainDuplicate()
{
    ToolChainState::ClearTools();
    ToolInstance first;
    first.type = 2;
    first.toolId = 501;
    first.label = "原始";
    ToolChainState::Tools().push_back(first);

    ToolInstance second;
    second.type = 7;
    second.toolId = 502;
    second.resultRoiSourceTool = 0;
    second.resultRoiSourceToolId = 501;
    ToolChainState::Tools().push_back(second);

    int inserted = -1;
    Require(ToolChainState::DuplicateTool(1, &inserted) && inserted == 2,
        "tool instance duplication failed");
    auto& tools = ToolChainState::Tools();
    Require(tools.size() == 3, "duplicated tool did not insert a new instance");
    Require(tools[2].toolId != tools[1].toolId,
        ("duplicated tool id was not unique: " + std::to_string(tools[1].toolId) +
            " vs " + std::to_string(tools[2].toolId)).c_str());
    Require(tools[2].label == "" || tools[2].label == tools[1].label,
        "duplicated tool parameters were not copied");
    Require(tools[2].resultRoiSourceTool == 0 && tools[2].resultRoiSourceToolId == 501,
        "duplicated tool dependency was not preserved");
    Require(ToolChainState::IndexOfToolId(501) == 0,
        "stable tool id lookup returned the wrong index");
    Require(ToolChainState::FindToolByIdReadOnly(502) == &tools[1],
        "read-only stable tool lookup returned the wrong instance");
    tools[1].groupName = "检测组";
    tools[2].groupName = "检测组";
    ToolChainState::SetGroupEnabled("检测组", false);
    Require(!tools[1].enabled && !tools[2].enabled && tools[0].enabled,
        "group enable operation changed the wrong tools");
    ToolChainState::SetAllEnabled(true);
    Require(tools[0].enabled && tools[1].enabled && tools[2].enabled,
        "batch enable operation did not update all tools");
    ToolChainState::SetGroupResultLabelsVisible("检测组", false);
    Require(tools[0].showResultLabels && !tools[1].showResultLabels &&
        !tools[2].showResultLabels,
        "group result-label operation changed the wrong tools");
    ToolChainState::SetAllResultLabelsVisible(true);
    ToolChainState::SetGroupStopOnFailure("检测组", true);
    Require(!tools[0].judgement.stopOnFailure && tools[1].judgement.stopOnFailure &&
        tools[2].judgement.stopOnFailure,
        "group stop-on-failure operation changed the wrong tools");
    ToolChainState::SetAllStopOnFailure(false);
    Require(!tools[0].judgement.stopOnFailure && !tools[1].judgement.stopOnFailure &&
        !tools[2].judgement.stopOnFailure,
        "batch stop-on-failure operation did not update all tools");

    tools[1].label = "复制源";
    tools[1].templateImg = cv::Mat(3, 3, CV_8UC1, cv::Scalar(4));
    tools[1].hasLastResult = true;
    tools[1].lastResult.success = true;
    Require(ToolChainState::CopyToolToClipboard(1) && ToolChainState::HasToolClipboard(),
        "tool clipboard copy failed");
    tools[1].label = "已修改";
    tools[1].templateImg.at<unsigned char>(0, 0) = 9;

    int pasted = -1;
    Require(ToolChainState::PasteToolAfter(2, &pasted) && pasted == 3,
        "tool clipboard paste failed");
    auto& pastedTools = ToolChainState::Tools();
    Require(pastedTools.size() == 4 && pastedTools[3].label == "复制源",
        "pasted tool parameters were not preserved");
    Require(!pastedTools[3].templateImg.empty() &&
        pastedTools[3].templateImg.at<unsigned char>(0, 0) == 4,
        "tool clipboard did not deep-copy template assets");
    Require(pastedTools[3].toolId != pastedTools[1].toolId &&
        !pastedTools[3].hasLastResult && pastedTools[3].toolImpl == nullptr,
        "pasted tool runtime state was not reset");
    int pastedAgain = -1;
    Require(ToolChainState::PasteToolAfter(3, &pastedAgain) && pastedAgain == 4 &&
        ToolChainState::Count() == 5,
        "tool clipboard did not support repeated paste");

    ToolChainState::ClearTools();
}

void TestExampleRecipesLoadAndExecute()
{
    const std::filesystem::path root = FindRepoRoot();
    const std::filesystem::path recipeDir = root / "docs" / "recipe_examples";
    const char* recipeNames[] = {
        "all_tools_test.recipe",
        "case_qr_clean.recipe",
        "case_qr_multi.recipe",
        "case_ocr.recipe",
        "case_measurement.recipe",
        "case_pipeline.recipe",
        "case_template_shape.recipe"
    };

    for (const char* recipeName : recipeNames)
    {
        RecipeData data;
        const std::filesystem::path recipePath = recipeDir / recipeName;
        Require(RecipeManager::Load(recipePath.string().c_str(), data),
            "example recipe failed to load");
        Require(!data.imagePath.empty() && !data.tools.empty(),
            "example recipe is missing image or tools");
        RecipeManager::Apply(data);

        std::string imagePath;
        Require(FrameNavigation::ConsumePendingImagePath(imagePath),
            "example recipe did not request its image");
        Require(std::filesystem::exists(std::filesystem::path(imagePath)),
            "example recipe image path was not resolved");
        Require(!ROIState::ReadOnlyItems().empty(),
            "example recipe did not restore ROI state");
        for (const ToolInstance& tool : ToolChainState::ReadOnlyTools())
            Require(!tool.label.empty(), "example recipe tool label is empty");

        if (std::string(recipeName) == "case_ocr.recipe")
        {
            const ToolInstance& ocr = ToolChainState::ReadOnlyTools().back();
            Require(std::filesystem::exists(std::filesystem::path(ocr.ocrDetModelPath)) &&
                std::filesystem::exists(std::filesystem::path(ocr.ocrRecModelPath)) &&
                std::filesystem::exists(std::filesystem::path(ocr.ocrDictionaryPath)),
                "OCR example model paths were not resolved");
        }
        if (std::string(recipeName) == "case_qr_clean.recipe")
        {
            const ToolInstance& qr = ToolChainState::ReadOnlyTools().back();
            Require(qr.judgement.enabled && qr.judgement.stopOnFailure,
                "QR example judgement settings regressed");
        }
        if (std::string(recipeName) == "case_template_shape.recipe")
        {
            const ToolInstance& shape = ToolChainState::ReadOnlyTools().back();
            Require(shape.resultRoiMode != 0 && shape.resultRoiSourceTool >= 0,
                "template/shape example result ROI settings regressed");
            Require(shape.fixture.enabled && shape.fixture.sourceToolIndex >= 0,
                "template/shape example fixture settings regressed");
        }
    }

    RecipeData pipeline;
    const std::filesystem::path pipelinePath = recipeDir / "case_pipeline.recipe";
    Require(RecipeManager::Load(pipelinePath.string().c_str(), pipeline),
        "pipeline example failed to reload");
    RecipeManager::Apply(pipeline);
    std::string imagePath;
    Require(FrameNavigation::ConsumePendingImagePath(imagePath),
        "pipeline example image was not requested");
    cv::Mat image = cv::imread(imagePath, cv::IMREAD_COLOR);
    Require(!image.empty(), "pipeline example image failed to decode");
    ImageState::SetImage(image);

    ToolController::RequestRunAll(false);
    const int maxTicks = static_cast<int>(ToolChainState::ReadOnlyTools().size()) + 4;
    for (int i = 0; i < maxTicks && ToolController::GetMode() != ToolController::Mode::Idle; ++i)
        ToolController::Tick();
    Require(ToolController::GetMode() == ToolController::Mode::Idle,
        "pipeline example execution did not finish");
    for (const ToolInstance& tool : ToolChainState::ReadOnlyTools())
    {
        if (tool.type != 12)
            Require(tool.hasLastResult, "pipeline example left a tool without a result");
    }

    ToolController::OnToolChainChanged();
    ToolChainState::ClearTools();
}

void TestToolControllerInputSourcesAndChainReset()
{
    ToolController::OnToolChainChanged();
    ToolChainState::ClearTools();
    ImageState::Clear();

    cv::Mat original(8, 8, CV_8UC1, cv::Scalar(10));
    ImageState::SetImage(original);

    int firstSeen = -1;
    int previousOutputSeen = -1;
    ToolInstance first;
    first.type = 2;
    first.inputSourceMode = 2;
    first.toolImpl = new TestInputCaptureTool(&firstSeen, 40);
    ToolChainState::Tools().push_back(std::move(first));

    ToolInstance second;
    second.type = 2;
    second.inputSourceMode = 1;
    second.toolImpl = new TestInputCaptureTool(&previousOutputSeen);
    ToolChainState::Tools().push_back(std::move(second));

    auto batchOutput = std::make_unique<TestModbusAdapter>();
    TestModbusAdapter* batchOutputView = batchOutput.get();
    Require(HardwareAdapterService::Register("batch-output", std::move(batchOutput)) &&
        batchOutputView->Connect({"127.0.0.1", 502, {}}).success,
        "batch hardware output setup failed");
    HardwareOutputBinding batchBinding;
    batchBinding.kind = HardwareOutputKind::ModbusCoil;
    batchBinding.adapterKey = "batch-output";
    batchBinding.address = 31;
    HardwareRuntimeService::ConfigureOutputBinding(batchBinding, true);

    ToolController::RequestRunAll(false);
    for (int i = 0; i < 8 && ToolController::GetMode() != ToolController::Mode::Idle; ++i)
        ToolController::Tick();
    Require(ToolController::GetMode() == ToolController::Mode::Idle,
        "batch execution did not finish");
    Require(firstSeen == 10, ("batch original-tool input fallback regressed: " + std::to_string(firstSeen)).c_str());
    Require(previousOutputSeen == 40, ("batch previous processed-image input regressed: " + std::to_string(previousOutputSeen)).c_str());
    Require(batchOutputView->lastAddress == 31 && batchOutputView->lastValue,
        "completed tool batch did not publish the aggregate Pass status");

    ImageState::Clear();
    batchOutputView->lastValue = true;
    ToolController::RequestRunAll(false);
    Require(!batchOutputView->lastValue,
        "tool-chain preflight failure did not publish Error/NG to hardware");

    HardwareRuntimeService::Shutdown();

    ToolController::OnToolChainChanged();
    ToolChainState::ClearTools();
    ImageState::SetImage(original);
    ImageState::SetDebugImage(cv::Mat(8, 8, CV_8UC1, cv::Scalar(70)));

    int standaloneOriginalSeen = -1;
    ToolInstance standaloneOriginal;
    standaloneOriginal.type = 2;
    standaloneOriginal.inputSourceMode = 2;
    standaloneOriginal.toolImpl = new TestInputCaptureTool(&standaloneOriginalSeen);
    ToolChainState::Tools().push_back(std::move(standaloneOriginal));
    ToolController::RequestRun(0);
    ToolController::Tick();
    Require(standaloneOriginalSeen == 10,
        "standalone original-tool input did not fall back to ImageState original");

    ImageState::SetDebugImage(cv::Mat(8, 8, CV_8UC1, cv::Scalar(70)));
    int standaloneProcessedSeen = -1;
    ToolChainState::Tools()[0].inputSourceMode = 1;
    delete ToolChainState::Tools()[0].toolImpl;
    ToolChainState::Tools()[0].toolImpl = new TestInputCaptureTool(&standaloneProcessedSeen);
    ToolController::RequestRun(0);
    ToolController::Tick();
    Require(standaloneProcessedSeen == 70,
        "standalone processed-image input regressed");

    ToolChainState::Tools()[0].hasLastResult = true;
    ToolChainState::Tools()[0].lastResult.toolName = "stale";
    gContext.unifiedResults.push_back(ToolResult{});
    ToolChainState::SetYoloLiveDetect(true);
    ToolChainState::SetYoloLiveInstanceIndex(0);
    ToolChainState::SetYoloLastTimeMs(12.0f);
    ToolController::RequestRunAll(true);
    ToolChainState::Tools()[0].hasLastResult = true;
    ToolChainState::Tools()[0].lastResult.toolName = "stale";
    ToolController::OnToolChainChanged();

    Require(ToolController::GetMode() == ToolController::Mode::Idle &&
        ToolController::GetCurrentIndex() == -1 && ToolController::GetToolTimeMs(0) == 0.0f,
        "tool chain change left controller queue/timing state");
    Require(!ToolChainState::Tools()[0].hasLastResult && gContext.unifiedResults.empty(),
        "tool chain change left stale result overlays");
    Require(!ToolChainState::YoloLiveDetect() && ToolChainState::YoloLiveInstanceIndex() == -1 &&
        ToolChainState::YoloLastTimeMs() == 0.0f,
        "tool chain change left stale live detection state");

    int disabledSeen = -1;
    ToolChainState::Tools()[0].enabled = false;
    delete ToolChainState::Tools()[0].toolImpl;
    ToolChainState::Tools()[0].toolImpl = new TestInputCaptureTool(&disabledSeen);
    ToolController::RequestRun(0);
    ToolController::Tick();
    Require(disabledSeen == -1 && ToolChainState::Tools()[0].hasLastResult &&
        ToolChainState::Tools()[0].lastResult.skipped,
        "disabled tool was executed instead of producing a skipped result");

    ToolChainState::ClearTools();
    ToolController::OnToolChainChanged();
}

void TestShapeMaxResultsDefaultIsOne()
{
    ToolInstance instance;
    RecipeToolInstance recipeTool;
    ShapeTool shapeTool;
    recipeTool.CaptureFrom(instance);

    Require(instance.shpMaxResults == 1, "shape tool UI default max results should be 1");
    Require(recipeTool.CreateToolInstance().shpMaxResults == 1,
        "shape recipe default max results should be 1");
    Require(shapeTool.maxResults == 1, "shape ITool default max results should be 1");
}

void TestToolInstanceLabelDefaultIsEmpty()
{
    ToolInstance instance;
    Require(instance.label.empty(), "tool instance label should default to empty");
}

void TestCoreStateOwnsRoiAndToolChain()
{
    ROIState::ClearInteraction();
    ROIState::CancelQueuedRestore();
    ToolChainState::ClearTools();

    ROI queued;
    queued.start = ImVec2(10.0f, 20.0f);
    queued.end = ImVec2(50.0f, 60.0f);
    queued.angle = 22.5f;
    ROIState::QueueRestoreAfterImageLoad({queued});
    Require(ROIState::HasQueuedRestore(), "recipe ROI restore was not queued");
    ROIState::ClearInteraction();
    Require(ROIState::ApplyQueuedRestore() && ROIState::ReadOnlyItems().size() == 1 &&
        std::abs(ROIState::ReadOnlyItems()[0].angle - 22.5f) < 0.001f,
        "recipe ROI restore after image load regressed");
    ROIState::CancelQueuedRestore();
    ROIState::ClearInteraction();

    ROI roi;
    roi.start = ImVec2(1.0f, 2.0f);
    roi.end = ImVec2(3.0f, 4.0f);
    ROIState::Add(roi, false);
    ROIState::SetSelectedIndex(0);

    Require(ROIState::ReadOnlyItems().size() == 1, "core ROI state did not store ROI");
    Require(ROIState::SelectedIndex() == 0, "core ROI selected index regressed");

    ToolInstance tool;
    tool.type = 7;
    tool.label = "core";
    ToolChainState::AddTool(tool);
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

    Require(ToolChainState::ReadOnlyTools().size() == 1, "core tool state did not retain tool");
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

void TestInspectionHistoryStatisticsAndCsv()
{
    InspectionHistory::Clear();
    ToolResult result;
    result.toolName = "width,caliper";
    result.status = ToolResultStatus::Pass;
    result.measurements.push_back({"width", 9.0, "mm"});
    InspectionHistory::AddResult(77, result.toolName, result, "2026-07-19 10:00:00");
    result.measurements[0].value = 10.0;
    InspectionHistory::AddResult(77, result.toolName, result, "2026-07-19 10:00:01");
    result.measurements[0].value = 11.0;
    InspectionHistory::AddResult(77, result.toolName, result, "2026-07-19 10:00:02");

    result.status = ToolResultStatus::Error;
    result.measurements[0].value = 100.0;
    InspectionHistory::AddResult(77, result.toolName, result, "2026-07-19 10:00:03");

    const InspectionHistory::Statistics statistics =
        InspectionHistory::Compute("width", 10.0, 3.0, 3.0);
    Require(statistics.count == 3, "SPC statistics did not exclude error samples");
    Require(std::abs(statistics.mean - 10.0) < 1.0e-9,
        "SPC mean regressed");
    Require(std::abs(statistics.standardDeviation - std::sqrt(2.0 / 3.0)) < 1.0e-9,
        "SPC standard deviation regressed");
    Require(statistics.cp > 1.2 && statistics.cpk > 1.2,
        "SPC Cp/Cpk regressed");
    const std::vector<std::string> names = InspectionHistory::MeasurementNames();
    Require(names.size() == 1 && names.front() == "width",
        "SPC measurement name query regressed");
    const std::vector<double> trend = InspectionHistory::Trend("width", 2);
    Require(trend.size() == 2 && trend[0] == 10.0 && trend[1] == 11.0,
        "SPC trend query regressed");

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "imgui_opencv_spc.csv";
    std::filesystem::remove(path);
    Require(InspectionHistory::ExportCsv(path.string().c_str()),
        "SPC CSV export failed");
    std::ifstream input(path, std::ios::binary);
    const std::string csv((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    Require(csv.find("sequence,timestamp,toolId") != std::string::npos,
        "SPC CSV header regressed");
    Require(csv.find("\"width,caliper\"") != std::string::npos,
        "SPC CSV escaping regressed");
    input.close();
    std::filesystem::remove(path);
    InspectionHistory::Clear();
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
    ImageState::SetImage(cv::Mat::zeros(80, 120, CV_8UC1));
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
    const auto oldResults = gContext.unifiedResults;

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

    result.sourceToolId = tool.toolId;
    gContext.unifiedResults = {result};
    Require(ResultOverlayState::Results().size() == 1 &&
        ResultOverlayState::Results().front().sourceToolId == tool.toolId,
        "result overlay state did not expose unified results");

    DetectedObject realtimeObject;
    realtimeObject.box = cv::Rect(1, 2, 3, 4);
    RealtimeDetectionState::SetObjects({realtimeObject});
    RealtimeDetectionState::SetOverlayVisible(true);
    RealtimeDetectionState::SetOverlayOffsetX(12.5f);
    Require(ResultOverlayState::IsRealtimeOverlayVisible() &&
        ResultOverlayState::RealtimeObjects().size() == 1 &&
        std::abs(ResultOverlayState::RealtimeOverlayOffsetX() - 12.5f) < 0.001f,
        "result overlay realtime snapshot regressed");

    tools.clear();
    ToolInstance source;
    source.toolId = 101;
    source.hasLastResult = true;
    ToolResult::Region fixtureRegion;
    fixtureRegion.bbox = cv::Rect(20, 30, 40, 20);
    fixtureRegion.angle = 15.0f;
    source.lastResult.regions.push_back(fixtureRegion);
    tools.push_back(source);

    ToolInstance follower;
    follower.hasLastResult = true;
    follower.showResultLabels = false;
    follower.fixture.enabled = true;
    follower.fixture.sourceToolId = source.toolId;
    follower.fixture.referenceOrigin = cv::Point2f(5.0f, 6.0f);
    follower.fixture.referenceAngleDegrees = 7.0f;
    tools.push_back(follower);

    const auto fixtureOverlays = ResultOverlayState::FixtureOverlays();
    Require(fixtureOverlays.size() == 1 && !fixtureOverlays.front().showLabel &&
        std::abs(fixtureOverlays.front().currentOrigin.x - 40.0f) < 0.001f &&
        std::abs(fixtureOverlays.front().currentOrigin.y - 40.0f) < 0.001f,
        "fixture overlay query did not resolve stable source tool id");

    ResultOverlayState::ClearResults();
    Require(ResultOverlayState::Results().empty() &&
        ResultOverlayState::RealtimeObjects().empty() &&
        !ResultOverlayState::IsRealtimeOverlayVisible(),
        "result overlay clear left stale unified or realtime results");

    settings = oldSettings;
    tools = oldTools;
    gContext.unifiedResults = oldResults;
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
            TestToolInstanceOwnsRecipeSerialization();
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
        TestRotatedROIExtractionAndResultRestore();
        TestToolExecutorUsesRotatedROI();
        TestGeometryDrawToolAndRecipe();
        TestYoloToolNoModelPath();
        TestQRCodeToolRecognizesBundledSample();
        TestRecursiveImageFolderScanSupportsCommonFormats();
        TestImageImportServiceAndDecodeFailures();
        TestToolJudgementPolicy();
        TestIndustrialMeasurement();
        TestCaliperOperators();
        TestCalibrationModel();
        TestFixtureTransform();
        TestResultROIResolution();
        TestTemplateMatchingToolUsesInstanceParameters();
        TestToolChainReorderRemapsResultROISource();
        TestRecipeRoundTrip();
        TestToolInstanceOwnsRecipeSerialization();
        TestSampleImageCorePipeline();
        TestLineToolSampleImage();
        TestMultiColorFinderNoPoints();
        TestOCRToolMissingEngineFailsWithTextResultContract();
        TestWindowsPPOCREngineUnavailableContract();
        TestWindowsPPOCRRecognitionCropKeepsHorizontalAspect();
#if defined(IMGUI_OPENCV_ENABLE_NCNN_OCR)
        TestWindowsPPOCREngineLoadsBundledModels();
#else
        std::cout << "regression_tests: skipped NCNN OCR model inference "
                     "(backend not enabled in this build)\n";
#endif
        TestWindowsPPOCREngineResolvesRelativeModelsFromReleaseDir();
#if defined(IMGUI_OPENCV_ENABLE_NCNN_OCR)
        TestOCRToolDefaultRelativeModelsWorkOutsideReleaseCwd();
#endif
        TestMorphologyToolITool();
        TestColorAnalyzerITool();
        TestBlobToolITool();
        TestDifferenceToolITool();
        TestROIEditorStateOwnsInteraction();
        TestToolAssetServiceOwnsCaptureWorkflow();
        TestToolROIServiceOwnsBoundROIEditing();
        TestHardwareAdapterServiceLifecycle();
        TestHardwareRuntimeAutomation();
        TestConcreteTcpTextAdapter();
        TestConcreteModbusTcpAdapterProtocol();
        TestConcreteOpenCvCameraAdapter();
        TestConcreteOpen62541OpcUaAdapter();
        TestModbusPlcTagMappingAdapter();
        TestCalibrationFitter();
        TestThresholdToolITool();
        TestEdgeToolITool();
        TestFrameSourceStateUpdatesCurrentFrame();
        TestImageStateOwnsCurrentImageSnapshot();
        TestImageViewStateOwnsTransform();
        TestRecipeCaptureUsesCurrentFramePath();
        TestToolExecutorInjectsImageSnapshot();
        TestToolExecutorResolvesMovedRuntimeRoi();
        TestToolChainEditActions();
        TestToolChainValidatorAndRunGuard();
        TestToolChainPreflight();
        TestToolChainDuplicate();
        TestToolControllerInputSourcesAndChainReset();
        TestExampleRecipesLoadAndExecute();
        TestShapeMaxResultsDefaultIsOne();
        TestToolInstanceLabelDefaultIsEmpty();
        TestCoreStateOwnsRoiAndToolChain();
        TestShapeMatcherTemplateLargerThanSearchImage();
        TestResultOverlayStatePolicy();
        TestInspectionHistoryStatisticsAndCsv();
        TestResultExporterWritesResultsAndReport();
        std::cout << "regression_tests: all tests passed\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "regression_tests: " << e.what() << "\n";
        return 1;
    }
}
