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
#include "../Algorithm/ToolImageUtils.h"
#include "../Algorithm/YOLOTool.h"
#include "../Algorithm/ShapeMatcher.h"
#include "../Algorithm/ShapeTools.h"
#include "../Algorithm/ContourDetector.h"
#include "../Algorithm/MultiColorFinder.h"
#include "../Algorithm/OCRTool.h"
#include "../Algorithm/QRCodeTool.h"
#include "../Algorithm/WindowsPPOCREngine.h"
#include "../Core/RecipeManager.h"
#include "../Core/RecipeAutosaveService.h"
#include "../Core/CalibrationModel.h"
#include "../Core/CalibrationFitter.h"
#include "../Core/FrameSourceState.h"
#include "../Core/FrameArchiveService.h"
#include "../Core/FrameNavigation.h"
#include "../Core/FixtureTransform.h"
#include "../Core/ImageState.h"
#include "../Core/ImageImportService.h"
#include "../Core/AsyncImageLoader.h"
#include "../Core/ImageViewState.h"
#include "../Core/HardwareAdapters.h"
#include "../Core/HikrobotMvsCameraAdapter.h"
#include "../Core/HuarayImvCameraAdapter.h"
#include "../Core/CameraPixelFormat.h"
#include "../Core/HardwareRuntimeService.h"
#include "../Core/HardwareSettingsService.h"
#include "../Core/ModbusTcpAdapter.h"
#include "../Core/TcpTextAdapter.h"
#include "../Core/ModbusPlcAdapter.h"
#include "../Core/OpenCvCameraAdapter.h"
#include "../Core/Open62541OpcUaAdapter.h"
#include "../Core/OpenFileDialog.h"
#include "../Core/ResultOverlayState.h"
#include "../Core/RenderBackend.h"
#include "../Core/RealtimeDetectionState.h"
#include "../Core/ResultROIResolver.h"
#include "../Core/ResultExporter.h"
#include "../Core/InspectionHistory.h"
#include "../Core/SpcDatabase.h"
#include "../Core/ToolExecutionGraph.h"
#include "../Core/ROIState.h"
#include "../Core/ROIEditorState.h"
#include "../Core/RotatedROI.h"
#include "../Core/ToolChainState.h"
#include "../Core/ToolChainValidator.h"
#include "../Core/ToolResultCapabilities.h"
#include "../Core/ToolResultUtils.h"
#include "../Core/ToolChainPreflight.h"
#include "../Core/ToolController.h"
#include "../Core/ToolAssetService.h"
#include "../Core/ToolROIService.h"
#include "../Core/ToolExecutor.h"
#include "../Core/ToolJudgement.h"
#include "../Core/VisionContext.h"
#include "../UI/ROIManager.h"
#include "../UI/RunResultLayout.h"
#include "../UI/ToolsWindow.h"
#include "../third_party/open62541/open62541.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/geometry/2d.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>

namespace
{
namespace fs = std::filesystem;

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
            server_ = CreateLoopbackServer(candidate);
            if (!server_)
                return false;

            UA_StatusCode status = AddNodes();
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
    static UA_Server* CreateLoopbackServer(std::uint16_t port)
    {
        UA_ServerConfig config{};
        config.logging = UA_Log_Stdout_new(UA_LOGLEVEL_ERROR);
        if (!config.logging)
            return nullptr;

        UA_StatusCode status = UA_ServerConfig_setMinimal(&config, port, nullptr);
        if (status != UA_STATUSCODE_GOOD)
        {
            // setMinimal owns cleanup of a partially initialized configuration.
            return nullptr;
        }

        const std::string endpointUrl =
            "opc.tcp://127.0.0.1:" + std::to_string(port);
        UA_String loopbackUrl = UA_STRING(
            const_cast<char*>(endpointUrl.c_str()));
        void* copiedUrls = nullptr;
        status = UA_Array_copy(&loopbackUrl, 1, &copiedUrls,
            &UA_TYPES[UA_TYPES_STRING]);
        if (status == UA_STATUSCODE_GOOD)
        {
            UA_Array_delete(config.serverUrls, config.serverUrlsSize,
                &UA_TYPES[UA_TYPES_STRING]);
            config.serverUrls = static_cast<UA_String*>(copiedUrls);
            config.serverUrlsSize = 1;

            // The production adapter currently connects anonymously. Keep that
            // contract explicit here, but expose this fixture on loopback only.
            if (config.sessionPKI.clear)
                config.sessionPKI.clear(&config.sessionPKI);
            config.sessionPKI = UA_CertificateVerification{};
            status = UA_AccessControl_default(&config, true,
                &UA_SECURITY_POLICY_NONE_URI, 0, nullptr);
        }

        if (status != UA_STATUSCODE_GOOD)
        {
            UA_ServerConfig_clean(&config);
            return nullptr;
        }
        return UA_Server_newWithConfig(&config);
    }

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
    TestInputCaptureTool(int* capturedValue, int outputValue = -1, int delayMs = 0)
        : captured(capturedValue), output(outputValue), delay(delayMs)
    {
    }

    const char* GetName() const override { return "input-capture"; }
    int GetType() const override { return 2; }
    ToolResult Execute(VisionContext& context) override
    {
        if (delay > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        ToolResult result;
        result.toolName = GetName();
        result.success = true;
        result.status = ToolResultStatus::Pass;
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
    int delay = 0;
};

struct TestCountingTool final : ITool
{
    explicit TestCountingTool(int* executions) : executions(executions) {}

    const char* GetName() const override { return "counting"; }
    int GetType() const override { return 2; }
    ToolResult Execute(VisionContext&) override
    {
        if (executions)
            ++*executions;
        ToolResult result;
        result.toolName = GetName();
        result.success = true;
        result.status = ToolResultStatus::Pass;
        return result;
    }
    void DrawUI() override {}
    nlohmann::json Save() const override { return {}; }
    void Load(const nlohmann::json&) override {}

    int* executions = nullptr;
};

struct TestOrderedTool final : ITool
{
    TestOrderedTool(std::vector<int>* order, int marker)
        : order(order), marker(marker) {}

    const char* GetName() const override { return "ordered"; }
    int GetType() const override { return 2; }
    ToolResult Execute(VisionContext&) override
    {
        if (order)
            order->push_back(marker);
        ToolResult result;
        result.toolName = GetName();
        result.status = ToolResultStatus::Pass;
        return result;
    }
    void DrawUI() override {}
    nlohmann::json Save() const override { return {}; }
    void Load(const nlohmann::json&) override {}

    std::vector<int>* order = nullptr;
    int marker = 0;
};

struct TestCameraAdapter final : ICameraAdapter
{
    explicit TestCameraAdapter(bool* disconnectedFlag) : disconnected(disconnectedFlag) {}
    const char* AdapterName() const override { return "test-camera"; }
    DeviceOperationResult Connect(const DeviceEndpoint&) override
    {
        ++connectCount;
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
        if (failGrabsRemaining > 0)
        {
            --failGrabsRemaining;
            state = DeviceConnectionState::Fault;
            return {false, "scripted grab failure"};
        }
        frame = cv::Mat(4, 6, CV_8UC1, cv::Scalar(17)).clone();
        return {true, {}};
    }
    DeviceOperationResult StartStream() override
    {
        ++startCount;
        state = DeviceConnectionState::Connected;
        return {true, {}};
    }
    void StopStream() override { stopped = true; }
    CameraCapabilities Capabilities() const override
    {
        return {true, true, true, true, true};
    }
    DeviceOperationResult ConfigureTrigger(const CameraTriggerConfig& config) override
    {
        triggerConfig = config;
        ++triggerConfigureCount;
        return {true, {}};
    }
    DeviceOperationResult ExecuteSoftwareTrigger() override
    {
        ++softwareTriggerCount;
        return triggerConfig.mode == CameraTriggerMode::Software
            ? DeviceOperationResult{true, {}}
            : DeviceOperationResult{false, "not in software trigger mode"};
    }
    DeviceOperationResult ConfigureBufferPolicy(CameraBufferPolicy policy) override
    {
        bufferPolicy = policy;
        ++bufferPolicyConfigureCount;
        return {true, {}};
    }

    DeviceConnectionState state = DeviceConnectionState::Disconnected;
    bool stopped = false;
    bool* disconnected = nullptr;
    int failGrabsRemaining = 0;
    int connectCount = 0;
    int startCount = 0;
    CameraTriggerConfig triggerConfig;
    int triggerConfigureCount = 0;
    int softwareTriggerCount = 0;
    CameraBufferPolicy bufferPolicy = CameraBufferPolicy::Sequential;
    int bufferPolicyConfigureCount = 0;
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
        ++connectCount;
        state = DeviceConnectionState::Connected;
        return {true, {}};
    }
    void Disconnect() override { state = DeviceConnectionState::Disconnected; }
    DeviceConnectionState ConnectionState() const override { return state; }
    std::string LastError() const override { return {}; }
    DeviceOperationResult ReadCoils(std::uint16_t address, std::uint16_t count,
        std::vector<bool>& values) override
    {
        std::lock_guard<std::mutex> lock(ioMutex);
        values.clear();
        values.reserve(count);
        for (std::uint16_t offset = 0; offset < count; ++offset)
        {
            const auto found = coilValues.find(
                static_cast<std::uint16_t>(address + offset));
            values.push_back(found != coilValues.end()
                ? found->second : nextCoilValue);
        }
        return {true, {}};
    }
    DeviceOperationResult WriteCoil(std::uint16_t address, bool value) override
    {
        std::lock_guard<std::mutex> lock(ioMutex);
        if (failWritesRemaining > 0)
        {
            --failWritesRemaining;
            state = DeviceConnectionState::Fault;
            return {false, "scripted write failure"};
        }
        lastAddress = address;
        lastValue = value;
        coilValues[address] = value;
        writeHistory.emplace_back(address, value);
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
    int failWritesRemaining = 0;
    int connectCount = 0;
    std::unordered_map<std::uint16_t, bool> coilValues;
    std::vector<std::pair<std::uint16_t, bool>> writeHistory;
    std::mutex ioMutex;
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

    ROI circle;
    circle.type = ROI_TYPE_CIRCLE;
    circle.start = ImVec2(50.0f, 60.0f);
    circle.end = ImVec2(70.0f, 60.0f);
    Require(circle.ToCvRect() == cv::Rect(30, 40, 40, 40),
        "circle ROI covering rectangle regressed");

    ROI polygonROI;
    polygonROI.type = ROI_TYPE_POLYGON;
    polygonROI.start = ImVec2(-999.0f, -999.0f); // Bounds must come from vertices.
    polygonROI.end = ImVec2(-998.0f, -998.0f);
    polygonROI.points = {{10.25f, 20.5f}, {35.75f, 18.25f}, {30.5f, 42.75f}};
    Require(polygonROI.ToCvRect() == cv::Rect(10, 18, 26, 25),
        "polygon ROI covering rectangle regressed");
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
    tool.blob.thresholdMode = 1;
    tool.blob.threshold = 127;
    tool.blob.minArea = 50;
    tool.blob.maxArea = 1000;
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

    ToolResult detectionResult;
    detectionResult.detections.push_back({cv::Rect(10, 20, 30, 40), 1, 0.9f, "part"});
    FixturePose detectionPose;
    Require(FixtureTransform::TryExtractPose(detectionResult, 0, detectionPose) &&
        cv::norm(detectionPose.origin - cv::Point2f(25.0f, 40.0f)) < 0.001f,
        "fixture pose extraction from detection failed");

    ToolResult lineResult;
    lineResult.lines.push_back({cv::Point(10, 10), cv::Point(30, 20), 22.36f, 26.565f});
    FixturePose linePose;
    Require(FixtureTransform::TryExtractPose(lineResult, 0, linePose) &&
        cv::norm(linePose.origin - cv::Point2f(20.0f, 15.0f)) < 0.001f &&
        std::abs(linePose.angleDegrees - 26.565f) < 0.001f,
        "fixture pose extraction from line failed");

    ToolResult textResult;
    textResult.texts.push_back({"SN123", cv::Rect(40, 50, 20, 10), 0.95f});
    FixturePose textPose;
    Require(FixtureTransform::TryExtractPose(textResult, 0, textPose) &&
        cv::norm(textPose.origin - cv::Point2f(50.0f, 55.0f)) < 0.001f,
        "fixture pose extraction from text box failed");
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
    const std::vector<ResultROIChoice> detectionChoices =
        ResultROIResolver::ListChoices(source, request);
    Require(detectionChoices.size() == 2 &&
        detectionChoices[0].label.find("检测框") != std::string::npos &&
        detectionChoices[0].label.find("中心(25.0,40.0)") != std::string::npos,
        "result ROI dropdown choices did not describe current detections");

    request.outputGeometry = ResultROIOutputGeometry::CenterPointsOrPreserveLines;
    resolution = ResultROIResolver::Resolve(source, request, cv::Size(100, 100));
    Require(resolution.available && resolution.rois.size() == 2 &&
        resolution.rois[0].type == ROI_TYPE_POINT &&
        resolution.rois[1].type == ROI_TYPE_POINT &&
        std::abs(resolution.rois[0].start.x - 25.0f) < 0.001f &&
        std::abs(resolution.rois[0].start.y - 40.0f) < 0.001f &&
        std::abs(resolution.rois[1].start.x - 60.0f) < 0.001f &&
        std::abs(resolution.rois[1].start.y - 65.0f) < 0.001f,
        "region results were not adapted to center-point ROIs");

    VisionContext measurementContext;
    measurementContext.rois = resolution.rois;
    MeasurementTool pointDistance;
    pointDistance.mode = 0;
    ToolResult distanceResult = pointDistance.Execute(measurementContext);
    Require(distanceResult.success && !distanceResult.measurements.empty() &&
        std::abs(distanceResult.measurements.front().value - std::sqrt(1850.0)) < 0.001,
        "result ROI center points were not accepted by point-distance measurement");

    ToolInstance firstSource;
    firstSource.type = 1;
    firstSource.toolId = 7101;
    firstSource.hasLastResult = true;
    ToolResult::Region firstRegion;
    firstRegion.bbox = cv::Rect(10, 20, 30, 40);
    firstSource.lastResult.regions.push_back(firstRegion);

    ToolInstance secondSource;
    secondSource.type = 2;
    secondSource.toolId = 7102;
    secondSource.hasLastResult = true;
    ToolResult::Region secondRegion;
    secondRegion.bbox = cv::Rect(50, 60, 20, 10);
    secondSource.lastResult.regions.push_back(secondRegion);

    ToolInstance pairMeasurement;
    pairMeasurement.type = 15;
    pairMeasurement.toolId = 7103;
    pairMeasurement.measureMode = 0;
    pairMeasurement.resultRoiMode = static_cast<int>(ResultROIMode::SelectedPair);
    pairMeasurement.resultRoiSourceToolId = firstSource.toolId;
    pairMeasurement.resultRoiSecondSourceToolId = secondSource.toolId;
    const std::vector<ToolInstance> pairTools = {
        firstSource, secondSource, pairMeasurement};
    const std::vector<int> pairIndices = {0, 1, 2};
    ToolInstance pairSnapshot;
    VisionContext pairContext;
    const cv::Mat pairImage(100, 100, CV_8UC1, cv::Scalar(0));
    Require(ToolExecutor::PrepareDetachedSnapshot(pairTools[2], pairImage, pairImage,
        2, pairTools, pairIndices, {}, -1, cv::Mat(), pairSnapshot, pairContext) &&
        pairContext.rois.size() == 2 &&
        pairContext.rois[0].type == ROI_TYPE_POINT &&
        pairContext.rois[1].type == ROI_TYPE_POINT,
        "selected results from two upstream tools were not combined as measurement points");
    ToolExecutor::ToolExecutionOutput pairOutput;
    Require(ToolExecutor::ExecuteDetached(pairSnapshot, pairContext, 2, pairOutput) &&
        pairOutput.result.success && !pairOutput.result.measurements.empty() &&
        std::abs(pairOutput.result.measurements.front().value - std::sqrt(1850.0)) < 0.001,
        "cross-upstream selected result pair measurement failed");

    request.outputGeometry = ResultROIOutputGeometry::Bounds;

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

    ToolResult lineSource;
    lineSource.lines.push_back({cv::Point(5, 8), cv::Point(25, 8), 20.0f, 0.0f});
    request = {};
    request.mode = ResultROIMode::AllResults;
    resolution = ResultROIResolver::Resolve(lineSource, request, cv::Size(40, 30));
    Require(resolution.available && resolution.rois.size() == 1 &&
        resolution.rois[0].ToCvRect() == cv::Rect(5, 8, 21, 1),
        "line result was not adapted to a downstream ROI");

    request.outputGeometry = ResultROIOutputGeometry::CenterPointsOrPreserveLines;
    resolution = ResultROIResolver::Resolve(lineSource, request, cv::Size(40, 30));
    Require(resolution.available && resolution.rois.size() == 1 &&
        resolution.rois[0].type == ROI_TYPE_LINE &&
        resolution.rois[0].start.x == 5.0f && resolution.rois[0].start.y == 8.0f &&
        resolution.rois[0].end.x == 25.0f && resolution.rois[0].end.y == 8.0f,
        "line result endpoints were not preserved for downstream measurement");

    ToolResult mixedSpatialSource;
    mixedSpatialSource.regions.push_back(firstRegion);
    mixedSpatialSource.lines = lineSource.lines;
    request.requireLineResults = true;
    const std::vector<ResultROIChoice> lineChoices =
        ResultROIResolver::ListChoices(mixedSpatialSource, request);
    Require(lineChoices.size() == 1 &&
        lineChoices[0].label.find("线段") != std::string::npos,
        "line-only result dropdown was masked by another spatial result channel");
    request.requireLineResults = false;

    mixedSpatialSource.detections.push_back(
        {cv::Rect(28, 4, 8, 9), 3, 0.77f, "混合检测"});
    mixedSpatialSource.texts.push_back(
        {"混合文本", cv::Rect(4, 20, 12, 5), 0.91f});
    request.channel = ToolSpatialResultChannel::Auto;
    const std::vector<ResultROIChoice> mixedChoices =
        ResultROIResolver::ListChoices(mixedSpatialSource, request);
    Require(mixedChoices.size() == 4,
        "mixed spatial output channels did not expose every selectable result");
    request.channel = ToolSpatialResultChannel::Texts;
    const std::vector<ResultROIChoice> textOnlyChoices =
        ResultROIResolver::ListChoices(mixedSpatialSource, request);
    Require(textOnlyChoices.size() == 1 &&
        textOnlyChoices[0].label.find("混合文本") != std::string::npos,
        "explicit result ROI channel selection did not isolate text results");
    request.channel = ToolSpatialResultChannel::Auto;

    request.outputGeometry = ResultROIOutputGeometry::Bounds;

    ToolResult textSource;
    textSource.texts.push_back({"CODE", cv::Rect(7, 9, 12, 6), 0.88f});
    resolution = ResultROIResolver::Resolve(textSource, request, cv::Size(40, 30));
    Require(resolution.available && resolution.rois.size() == 1 &&
        resolution.rois[0].ToCvRect() == cv::Rect(7, 9, 12, 6),
        "text result was not adapted to a downstream ROI");

    const std::array<bool, 18> expectedSpatial = {
        false, true, true, false, true, true, true, true, false,
        false, true, true, false, true, true, true, true, true,
    };
    for (int type = 0; type < static_cast<int>(expectedSpatial.size()); ++type)
    {
        Require(ToolCapabilitiesForType(type).SupportsSpatialResult() ==
            expectedSpatial[type], "tool result capability matrix regressed");
        Require(ToolResultKindsLabel(type) != nullptr &&
            ToolResultKindsLabel(type)[0] != '\0',
            "tool result capability label is missing");
    }
}

void TestAllToolRegistryAndResultCapabilityContracts()
{
    for (int type = 0; type <= 17; ++type)
    {
        const ToolResultCapabilities capabilities = ToolCapabilitiesForType(type);
        Require(capabilities.regions || capabilities.detections || capabilities.lines ||
                capabilities.texts || capabilities.measurements || capabilities.processedImage,
            "tool result capability table contains an empty tool contract");
        Require(ToolResultKindsLabel(type) != nullptr &&
                std::string(ToolResultKindsLabel(type)) != "未知输出",
            "tool result capability label is missing");

        // 原图（type 12）由执行器直接恢复本轮输入，不创建 ITool 对象。
        if (type == 12)
            continue;

        std::unique_ptr<ITool> tool = ITool::Create(type);
        Require(tool != nullptr, "tool registry cannot create a catalog tool");
        Require(tool->GetType() == type, "tool factory returned the wrong type");
        Require(tool->GetName() != nullptr && tool->GetName()[0] != '\0',
            "tool factory returned an empty display name");
        Require(std::string(ToolRegistry::GetName(type)) != "Unknown",
            "tool registry is missing the Chinese catalog name");
    }
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

    cv::Mat fractionalSource = cv::Mat::zeros(context.image.size(), CV_8UC1);
    const cv::Mat translation = cv::Mat(cv::Matx23d(
        1.0, 0.0, 55.35,
        0.0, 1.0, 35.65));
    cv::warpAffine(tool.templateImg, fractionalSource, translation,
        fractionalSource.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT,
        cv::Scalar(0));
    context.image = fractionalSource;
    tool.subpixelRefinement = true;
    tool.matchThreshold = 0.70f;
    result = tool.Execute(context);
    Require(result.success && result.regions.size() == 1 &&
        cv::norm(result.regions[0].center - cv::Point2f(67.35f, 44.65f)) < 0.65f,
        "HALCON-style subpixel template position refinement regressed");
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
    consumer.resultRoiMode = 3;
    consumer.resultRoiSourceTool = 0;
    consumer.resultRoiSecondSourceTool = 0;
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
    tools[1].resultRoiSecondSourceToolId = detectorId;
    tools[1].fixture.sourceToolId = detectorId;
    ToolChainState::SetActiveIndex(1);
    ToolChainState::SetYoloLiveInstanceIndex(0);

    ToolChainState::MoveOriginalToolToFront();
    Require(tools[0].type == 12 && tools[2].type == 2,
        "original tool reorder regressed");
    Require(tools[2].resultRoiSourceTool == 1,
        "result ROI source index was not remapped after reorder");
    Require(tools[2].resultRoiSecondSourceTool == 1,
        "second result ROI source index was not remapped after reorder");
    Require(tools[2].fixture.sourceToolIndex == 1,
        "fixture source index was not remapped after reorder");
    Require(tools[2].resultRoiSourceToolId == detectorId &&
        tools[2].resultRoiSecondSourceToolId == detectorId &&
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
    data.loopIntervalMs = 375;
    data.threshold.useGray = true;
    data.threshold.thresholdValue = 123;
    data.tmMatch.maxResults = 3;
    data.tmMatch.matchThreshold = 0.91f;
    data.rois.push_back({1.0f, 2.0f, 30.0f, 40.0f, 27.5f, 0});
    data.rois[0].type = ROI_TYPE_POLYGON;
    data.rois[0].points = {{1.0f, 2.0f}, {30.0f, 4.0f}, {20.0f, 40.0f}};
    data.taskGroups.push_back({"检测组", true, "assets/images/task-a.jpg"});
    data.taskGroups.push_back({"空任务", false});
    data.taskGroups[0].imageFolderPath = "assets/images/task-a";
    data.taskGroups[0].imageFolderIndex = 3;
    data.taskGroups[0].imageFolderCount = 8;
    data.taskGroups[0].cameraPreferred = true;
    data.taskGroups[0].cameraIndex = 15;

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
    tool.showResultLabels = false;
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
    qr.showResultLabels = false;
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
    std::ifstream savedRecipeStream(path);
    nlohmann::json savedRecipe;
    savedRecipeStream >> savedRecipe;
    savedRecipeStream.close();
    Require(savedRecipe.value("version", 0) == 5,
        "new recipes were not saved with schema version 5");
    for (const auto& savedTool : savedRecipe["tools"])
    {
        Require(!savedTool.contains("blobShowLabels") &&
            !savedTool.contains("cntShowLabels") &&
            !savedTool.contains("shpShowLabels") &&
            !savedTool.contains("lineShowLabels") &&
            !savedTool.contains("qrShowText") &&
            !savedTool.contains("differenceShowLabels"),
            "recipe v3 still serialized a legacy result-label field");
    }

    RecipeData loaded;
    Require(RecipeManager::Load(path.string().c_str(), loaded), "recipe load failed");

    Require(loaded.name == data.name, "recipe name round-trip regressed");
    Require(loaded.loopIntervalMs == 375, "loop interval recipe round-trip regressed");
    Require(loaded.threshold.useGray == data.threshold.useGray, "threshold round-trip regressed");
    Require(loaded.threshold.thresholdValue == data.threshold.thresholdValue, "threshold value round-trip regressed");
    Require(loaded.rois.size() == 1 && loaded.rois[0].endX == 30.0f &&
        std::abs(loaded.rois[0].angle - 27.5f) < 0.001f &&
        loaded.rois[0].points.size() == 3 &&
        std::abs(loaded.rois[0].points[2].y - 40.0f) < 0.001f,
        "ROI round-trip regressed");
    Require(loaded.taskGroups.size() == 2 && loaded.taskGroups[0].name == "检测组" &&
        loaded.taskGroups[0].enabled &&
        loaded.taskGroups[0].imagePath == "assets/images/task-a.jpg" &&
        loaded.taskGroups[0].imageFolderPath == "assets/images/task-a" &&
        loaded.taskGroups[0].imageFolderIndex == 3 &&
        loaded.taskGroups[0].imageFolderCount == 8 &&
        loaded.taskGroups[0].cameraPreferred &&
        loaded.taskGroups[0].cameraIndex == 15 &&
        loaded.taskGroups[1].name == "空任务" &&
        !loaded.taskGroups[1].enabled, "task-group order/state round-trip regressed");
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
        loadedTool.differenceMorphKernelSize == 5 && !loadedTool.showResultLabels,
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
    Require(!loadedQr.showResultLabels, "QR label flag round-trip regressed");
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
        legacy["rois"][0].erase("points");
        legacy["tools"][0]["searchROIs"][0].erase("angle");
        std::ofstream output(legacyPath);
        output << legacy.dump(2);
    }
    RecipeData legacyLoaded;
    Require(RecipeManager::Load(legacyPath.string().c_str(), legacyLoaded),
        "legacy ROI recipe load failed");
    const ToolInstance legacyTool = legacyLoaded.tools[0].CreateToolInstance();
    Require(legacyLoaded.rois.size() == 1 && legacyLoaded.rois[0].angle == 0.0f &&
        legacyLoaded.rois[0].points.empty() &&
        legacyTool.searchROIs.size() == 1 && legacyTool.searchROIs[0].angle == 0.0f,
        "legacy recipe ROI angle did not default to zero");

    const std::filesystem::path futurePath =
        std::filesystem::temp_directory_path() / "imgui_opencv_future.recipe";
    {
        std::ifstream input(path);
        nlohmann::json future = nlohmann::json::parse(input);
        future["version"] = 999;
        future["futureOnlyField"] = "must-not-be-discarded";
        std::ofstream output(futurePath);
        output << future.dump(2);
    }
    RecipeData protectedData;
    protectedData.name = "unchanged";
    Require(!RecipeManager::Load(futurePath.string().c_str(), protectedData),
        "newer recipe version was not rejected");
    Require(protectedData.name == "unchanged",
        "newer recipe version partially modified destination data");

    std::filesystem::remove(path);
    std::filesystem::remove(legacyPath);
    std::filesystem::remove(futurePath);
    std::filesystem::remove(path.parent_path() / "imgui_opencv_regression_tool.png");
    std::filesystem::remove(path.parent_path() / "imgui_opencv_regression_difference.png");
}

void TestTaskGroupManagement()
{
    ToolChainState::ClearTools();
    Require(ToolChainState::ReadOnlyTaskGroups().empty(),
        "an empty recipe unexpectedly created a task group");
    const int onlyGroup = ToolChainState::CreateTaskGroup();
    Require(onlyGroup == 0 && ToolChainState::RemoveTaskGroup(onlyGroup) &&
        ToolChainState::ReadOnlyTaskGroups().empty(),
        "removing the last task group did not leave the task list empty");

    ToolResult measurementOverlay;
    measurementOverlay.toolName = "工业测量";
    measurementOverlay.measurements.push_back({"value", 270.0, "mm"});
    Require(BuildToolResultLineOverlayLabel(measurementOverlay) ==
            "工业测量 270.000 mm",
        "measurement line overlay omitted its value or unit");

    ToolInstance first;
    first.type = 0;
    first.groupName = "旧任务A";
    ToolChainState::AddTool(std::move(first));
    ToolInstance second;
    second.type = 2;
    ToolChainState::AddTool(std::move(second));
    ToolInstance third;
    third.type = 7;
    third.groupName = "旧任务B";
    ToolChainState::AddTool(std::move(third));

    Require(ToolChainState::ReadOnlyTaskGroups().size() == 2 &&
        ToolChainState::ReadOnlyTaskGroups()[0].name == "旧任务A" &&
        ToolChainState::ReadOnlyTaskGroups()[1].name == "旧任务B",
        "legacy tool groups were not imported in tool order");

    std::vector<TaskGroupDefinition> restoredGroups;
    restoredGroups.push_back({0, "旧任务B", false});
    restoredGroups.push_back({0, "旧任务A", true});
    restoredGroups.push_back({0, "空任务", true});
    ToolChainState::ReplaceTaskGroups(std::move(restoredGroups));
    Require(ToolChainState::ReadOnlyTaskGroups().size() == 3 &&
        ToolChainState::ReadOnlyTaskGroups()[0].name == "旧任务B" &&
        !ToolChainState::AtReadOnly(2)->enabled,
        "task-group order or disabled state was not restored");

    const int created = ToolChainState::CreateTaskGroup();
    Require(created == 3 && ToolChainState::ReadOnlyTaskGroups()[created].name == "任务01",
        "automatic task-group naming regressed");
    Require(ToolChainState::AssignToolToTaskGroup(1, created) &&
        ToolChainState::AtReadOnly(1)->groupName == "任务01",
        "tool assignment to task group regressed");
    Require(ToolChainState::RenameTaskGroup(created, "新任务") &&
        ToolChainState::AtReadOnly(1)->groupName == "新任务",
        "task-group rename did not update assigned tools");

    ToolInstance sameTaskTool;
    sameTaskTool.type = 9;
    sameTaskTool.groupName = "新任务";
    const int sameTaskIndex = ToolChainState::AddTool(std::move(sameTaskTool));
    const std::uint64_t firstTaskToolId = ToolChainState::AtReadOnly(1)->toolId;
    const std::uint64_t secondTaskToolId =
        ToolChainState::AtReadOnly(sameTaskIndex)->toolId;
    const std::uint64_t otherTaskToolId = ToolChainState::AtReadOnly(2)->toolId;
    Require(ToolChainState::MoveToolWithinTaskGroup(sameTaskIndex, -1) &&
        ToolChainState::AtReadOnly(1)->toolId == secondTaskToolId &&
        ToolChainState::AtReadOnly(2)->toolId == otherTaskToolId &&
        ToolChainState::AtReadOnly(sameTaskIndex)->toolId == firstTaskToolId,
        "task-local tool move changed another task or used global adjacency");
    Require(ToolChainState::MoveTaskGroup(created, 0) &&
        ToolChainState::ReadOnlyTaskGroups()[0].name == "新任务",
        "task-group ordering regressed");
    ToolChainState::SetAllEnabled(false);
    Require(std::all_of(ToolChainState::ReadOnlyTaskGroups().begin(),
            ToolChainState::ReadOnlyTaskGroups().end(),
            [](const TaskGroupDefinition& group) { return !group.enabled; }) &&
        std::all_of(ToolChainState::ReadOnlyTools().begin(),
            ToolChainState::ReadOnlyTools().end(),
            [](const ToolInstance& tool) { return !tool.enabled; }),
        "bulk disable did not synchronize task groups and tools");
    ToolChainState::SetAllEnabled(true);
    Require(std::all_of(ToolChainState::ReadOnlyTaskGroups().begin(),
            ToolChainState::ReadOnlyTaskGroups().end(),
            [](const TaskGroupDefinition& group) { return group.enabled; }) &&
        std::all_of(ToolChainState::ReadOnlyTools().begin(),
            ToolChainState::ReadOnlyTools().end(),
            [](const ToolInstance& tool) { return tool.enabled; }),
        "bulk enable did not synchronize task groups and tools");
    Require(ToolChainState::RemoveTaskGroup(0) &&
        ToolChainState::Count() == 2 &&
        ToolChainState::FindToolByIdReadOnly(firstTaskToolId) == nullptr &&
        ToolChainState::FindToolByIdReadOnly(secondTaskToolId) == nullptr &&
        ToolChainState::FindToolByIdReadOnly(otherTaskToolId) != nullptr,
        "removing a task group did not delete only its assigned tools");
    Require(ToolChainState::MaximumTaskGroups() == 16,
        "task-group capacity is not 16");
    while (ToolChainState::ReadOnlyTaskGroups().size() <
        ToolChainState::MaximumTaskGroups())
    {
        Require(ToolChainState::CreateTaskGroup() >= 0,
            "creating one of 16 task groups was rejected");
    }
    Require(ToolChainState::ReadOnlyTaskGroups().size() == 16 &&
        ToolChainState::CreateTaskGroup() == -1,
        "task-group capacity did not stop exactly at 16");
    ToolChainState::ClearTools();
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

    tool.params.colorSpace = 4;
    result = entry->Execute(ctx);
    Require(result.success && std::abs(result.measurements[0].value - 22.0) < 0.001,
        "color analyzer Gray mode did not convert BGR to luminance");
    Require(std::abs(result.measurements[1].value) < 0.001 &&
            std::abs(result.measurements[2].value) < 0.001,
        "color analyzer Gray mode unexpectedly retained color channels");
}

void TestContourDirectionAndSubpixelBoundary()
{
    cv::Mat image = cv::Mat::zeros(128, 128, CV_8UC1);
    cv::circle(image, cv::Point(64, 64), 42, cv::Scalar(255), cv::FILLED, cv::LINE_AA);
    cv::circle(image, cv::Point(64, 64), 18, cv::Scalar(0), cv::FILLED, cv::LINE_AA);

    ContourDetector::Params params;
    params.blurSize = 0;
    params.threshMode = 1;
    params.threshValue = 128;
    params.retrMode = 2;
    params.approxMethod = 0;
    params.minArea = 20.0;
    params.normalizeDirection = true;
    params.subpixelBoundary = true;
    const auto contours = ContourDetector::Detect(image, params);

    const auto external = std::find_if(contours.begin(), contours.end(),
        [](const ContourResult& contour) { return !contour.isHole; });
    const auto hole = std::find_if(contours.begin(), contours.end(),
        [](const ContourResult& contour) { return contour.isHole; });
    Require(external != contours.end() && hole != contours.end(),
        "contour hierarchy did not preserve external and hole boundaries");
    Require(external->signedArea > 0.0 && hole->signedArea < 0.0,
        "contour direction normalization did not distinguish external and hole boundaries");
    Require(external->subpixelPoints.size() == external->points.size(),
        "subpixel contour point count regressed");
    const bool hasFractionalBoundary = std::any_of(external->subpixelPoints.begin(),
        external->subpixelPoints.end(), [](const cv::Point2f& point) {
            return std::abs(point.x - std::round(point.x)) > 1.0e-3f ||
                std::abs(point.y - std::round(point.y)) > 1.0e-3f;
        });
    Require(hasFractionalBoundary,
        "subpixel contour refinement returned integer-only boundary points");
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
    source.mcfShowPreview = false;
    source.resultRoiMode = 2;
    source.resultRoiSourceToolId = 1234;
    source.resultRoiSecondSourceTool = 3;
    source.resultRoiSecondSourceToolId = 4321;
    source.resultRoiSecondIndex = 4;
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
    source.MarkParametersChanged();
    const std::uint64_t sourceRevision = source.parameterRevision;

    ROI polygon;
    polygon.type = ROI_TYPE_POLYGON;
    polygon.start = {1.0f, 2.0f};
    polygon.end = {20.0f, 30.0f};
    polygon.points = {{1.0f, 2.0f}, {20.0f, 2.0f}, {10.0f, 30.0f}};
    source.searchROIs.push_back(polygon);

    const nlohmann::json serialized = source.ToRecipeJson();
    Require(!serialized.contains("parametersDirty"),
        "runtime parameter dirty state leaked into recipe JSON");
    Require(!serialized.contains("parameterRevision"),
        "runtime parameter revision leaked into recipe JSON");
    ToolInstance loaded;
    loaded.LoadRecipeJson(serialized);

    Require(loaded.type == source.type && loaded.toolId == source.toolId && !loaded.enabled,
        "ToolInstance identity recipe serialization regressed");
    Require(loaded.label == source.label && !loaded.showResultLabels &&
        !loaded.showTemplatePreview && !loaded.mcfShowPreview,
        "ToolInstance display recipe serialization regressed");
    Require(loaded.fixture.sourceToolId == 5678 && loaded.judgement.stopOnFailure &&
        loaded.resultRoiSecondSourceTool == 3 &&
        loaded.resultRoiSecondSourceToolId == 4321 &&
        loaded.resultRoiSecondIndex == 4,
        "ToolInstance dependency/judgement serialization regressed");
    Require(loaded.searchROIs.size() == 1 && loaded.searchROIs[0].points.size() == 3,
        "ToolInstance polygon ROI serialization regressed");
    Require(loaded.measureCaliperCount == 24 &&
        std::abs(loaded.measureMinimumConfidence - 0.82f) < 0.0001f &&
        loaded.measureCalibrationSamples.size() == 1,
        "ToolInstance measurement serialization regressed");
    Require(serialized.contains("settings") &&
        serialized["settings"].contains("templateMatch") &&
        serialized["settings"].contains("yolo") &&
        serialized["settings"].contains("ocr"),
        "ToolInstance settings groups were not serialized");
    Require(std::abs(loaded.yolo.confidenceThreshold - loaded.yoloConfThreshold) < 0.0001f &&
        loaded.ocr.detectionModelPath == loaded.ocrDetModelPath &&
        loaded.templateMatch.maxResults == loaded.maxResults,
        "ToolInstance settings groups were not synchronized with v3 fields");

    ToolInstance legacyBlob;
    legacyBlob.LoadRecipeJson({{"type", 2}, {"blobShowLabels", false}});
    Require(!legacyBlob.showResultLabels,
        "legacy per-tool label setting did not migrate to the common switch");

    RecipeToolInstance legacyRecipeTool;
    legacyRecipeTool.LoadToolJson({{"type", 14}, {"qrShowText", false}});
    const nlohmann::json migratedRecipeTool = legacyRecipeTool.ToJson();
    Require(!migratedRecipeTool.value("showResultLabels", true) &&
        !migratedRecipeTool.contains("qrShowText"),
        "recipe v3 migration kept the legacy per-tool label field");

    ToolInstance finderSource;
    finderSource.type = 10;
    finderSource.toolImpl = ITool::Create(10);
    auto* sourceFinder = dynamic_cast<MultiColorFinder*>(finderSource.toolImpl.get());
    Require(sourceFinder != nullptr, "multi-color finder factory setup failed");
    ColorPoint point;
    point.r = 10;
    point.g = 20;
    point.b = 30;
    point.tolerance = 14;
    sourceFinder->points.push_back(point);
    ToolInstance finderCopy = finderSource;
    auto* copiedFinder = dynamic_cast<MultiColorFinder*>(finderCopy.toolImpl.get());
    Require(copiedFinder && copiedFinder != sourceFinder &&
        copiedFinder->points.size() == 1 && copiedFinder->points[0].tolerance == 14,
        "ToolInstance did not deep-copy owned algorithm parameters");
    ToolInstance runtimeCopy = source;
    Require(runtimeCopy.parameterRevision == sourceRevision,
        "ToolInstance copy lost the runtime parameter revision");
    sourceFinder->points[0].tolerance = 2;
    Require(copiedFinder->points[0].tolerance == 14,
        "copied ToolInstance shared mutable algorithm state");

    loaded.hasLastResult = true;
    loaded.measureRuntimeROIIds.push_back(42);
    const std::uint64_t loadedRevision = loaded.parameterRevision;
    loaded.ClearRuntimeState();
    Require(!loaded.hasLastResult && !loaded.parametersDirty &&
        loaded.measureRuntimeROIIds.empty() && loaded.parameterRevision == loadedRevision,
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
    ROIEditorState::ActivePointIndex() = 2;
    ROIEditorState::PolygonDraftPoints().push_back(ImVec2(3.0f, 4.0f));
    ROIEditorState::ResetInteraction();
    Require(ROIEditorState::ActivePointIndex() == -1 &&
        ROIEditorState::PolygonDraftPoints().empty(),
        "ROI editor polygon interaction reset regressed");

    ROIState::ClearInteraction();
    ROI editable;
    editable.type = ROI_TYPE_RECT;
    editable.start = ImVec2(10.0f, 10.0f);
    editable.end = ImVec2(30.0f, 30.0f);
    editable.locked = true;
    editable.visible = false;
    editable.constrainToImage = true;
    ROIEditorState::EnsureRuntimeId(editable);
    const int editableIndex = ROIState::Add(editable, true);
    ROIState::BeginHistoryTransaction();
    editable.start.x = 20.0f;
    ROIState::Update(editableIndex, editable);
    editable.start.x = 25.0f;
    ROIState::Update(editableIndex, editable);
    ROIState::CommitHistoryTransaction();
    Require(ROIState::Undo() && std::abs(ROIState::At(editableIndex)->start.x - 10.0f) < 0.001f,
        "ROI transaction undo did not restore the pre-drag geometry");
    Require(ROIState::Redo() && std::abs(ROIState::At(editableIndex)->start.x - 25.0f) < 0.001f,
        "ROI transaction redo did not restore the edited geometry");

    ToolInstance roiPersistence;
    roiPersistence.searchROIs.push_back(editable);
    ToolInstance restoredRoiPersistence;
    restoredRoiPersistence.LoadRecipeJson(roiPersistence.ToRecipeJson());
    Require(restoredRoiPersistence.searchROIs.size() == 1 &&
            restoredRoiPersistence.searchROIs[0].locked &&
            !restoredRoiPersistence.searchROIs[0].visible &&
            restoredRoiPersistence.searchROIs[0].constrainToImage,
        "ROI lock/visibility/bounds recipe persistence regressed");

    ROI outside = editable;
    outside.locked = false;
    outside.visible = true;
    outside.start = ImVec2(-20.0f, -10.0f);
    outside.end = ImVec2(140.0f, 120.0f);
    outside.angle = 25.0f;
    outside.ClampToImage(cv::Size(100, 80));
    for (const ImVec2& corner : outside.Corners())
        Require(corner.x >= -0.01f && corner.y >= -0.01f &&
                corner.x <= 100.01f && corner.y <= 80.01f,
            "ROI image-bound constraint left a rotated corner outside the image");
    ROIState::ClearInteraction();
}

void TestHalconStyleROIDomain()
{
    cv::Mat otsuInput(10, 10, CV_8UC1, cv::Scalar(100));
    cv::Mat otsuMask = cv::Mat::zeros(10, 10, CV_8UC1);
    otsuMask(cv::Rect(2, 2, 6, 6)).setTo(255);
    otsuInput(cv::Rect(2, 2, 3, 6)).setTo(20);
    otsuInput(cv::Rect(5, 2, 3, 6)).setTo(70);
    Require(std::abs(ToolImageUtils::MaskedOtsuThreshold(otsuInput, otsuMask) - 20.0) < 0.001,
        "OTSU threshold included pixels outside the HALCON definition domain");

    cv::Mat policyMask = cv::Mat::zeros(10, 10, CV_8UC1);
    policyMask(cv::Rect(0, 0, 5, 10)).setTo(255);
    const cv::Rect crossingBox(4, 2, 4, 4);
    Require(!ToolImageUtils::AcceptRectByDomain(policyMask, crossingBox, 0, 0.5f) &&
             ToolImageUtils::AcceptRectByDomain(policyMask, crossingBox, 1, 0.5f) &&
            !ToolImageUtils::AcceptRectByDomain(policyMask, crossingBox, 2, 0.5f) &&
             ToolImageUtils::AcceptRectByDomain(policyMask, crossingBox, 3, 0.2f) &&
            !ToolImageUtils::AcceptRectByDomain(policyMask, crossingBox, 3, 0.5f),
        "ROI center/intersection/containment/coverage policies regressed");

    ToolInstance policyTool;
    policyTool.roiResultPolicy = 3;
    policyTool.roiMinimumCoverage = 0.65f;
    ToolInstance restoredPolicyTool;
    restoredPolicyTool.LoadRecipeJson(policyTool.ToRecipeJson());
    Require(restoredPolicyTool.roiResultPolicy == 3 &&
            std::abs(restoredPolicyTool.roiMinimumCoverage - 0.65f) < 0.001f,
        "ROI result policy recipe persistence regressed");

    cv::Mat morphInput = cv::Mat::zeros(9, 9, CV_8UC1);
    cv::Mat morphMask = cv::Mat::zeros(9, 9, CV_8UC1);
    morphMask(cv::Rect(2, 2, 5, 5)).setTo(255);
    morphInput.setTo(100, morphMask);
    MorphologyTool::Params morphParams;
    morphParams.opType = 0;
    morphParams.kernelSize = 1;
    morphParams.iterations = 1;
    cv::Mat morphResult = MorphologyTool::Process(morphInput, morphParams, morphMask);
    Require(morphResult.at<uchar>(2, 2) == 100 && morphResult.at<uchar>(1, 1) == 0,
        "morphology domain boundary was influenced by outside pixels");

    ROI rectangle2;
    rectangle2.type = ROI_TYPE_RECT;
    rectangle2.start = ImVec2(20.0f, 30.0f);
    rectangle2.end = ImVec2(80.0f, 70.0f);
    rectangle2.angle = 135.0f;
    Require(std::abs(rectangle2.HalconRow() - 50.0f) < 0.001f &&
            std::abs(rectangle2.HalconColumn() - 50.0f) < 0.001f,
        "HALCON rectangle2 center conversion regressed");
    Require(std::abs(rectangle2.HalconLength1() - 30.0f) < 0.001f &&
            std::abs(rectangle2.HalconLength2() - 20.0f) < 0.001f,
        "HALCON rectangle2 half-length conversion regressed");
    Require(std::abs(rectangle2.HalconPhi() + CV_PI * 0.25) < 0.0001,
        "HALCON rectangle2 Phi normalization regressed");

    cv::Mat source(120, 120, CV_8UC3, cv::Scalar(0, 0, 0));
    ROI circle;
    circle.type = ROI_TYPE_CIRCLE;
    circle.start = ImVec2(60.0f, 60.0f);
    circle.end = ImVec2(80.0f, 60.0f);
    cv::circle(source, cv::Point(60, 60), 20, cv::Scalar(255, 255, 255), cv::FILLED);
    cv::circle(source, cv::Point(60, 60), 3, cv::Scalar(0, 0, 0), cv::FILLED);

    cv::Mat circleCrop;
    RotatedROI::Transform circleTransform;
    Require(RotatedROI::Extract(source, circle, circleCrop, circleTransform),
        "circle ROI domain extraction failed");
    Require(circleTransform.domainMask.at<uchar>(0, 0) == 0 &&
            circleTransform.domainMask.at<uchar>(20, 20) != 0,
        "circle ROI domain includes its bounding-box corners");
    Require(circle.Contains(ImVec2(60.0f, 60.0f)) &&
            !circle.Contains(ImVec2(42.0f, 42.0f)),
        "circle ROI exact containment regressed");

    VisionContext context;
    context.image = source;
    context.originalImage = source;
    context.width = source.cols;
    context.height = source.rows;
    context.rois = {circle};
    context.selectedROI = 0;
    ToolInstance blob;
    blob.type = 2;
    blob.toolId = 7101;
    blob.blob.thresholdMode = 1;
    blob.blob.threshold = 127;
    blob.blob.invert = true;
    blob.blob.minArea = 5;
    blob.blob.maxArea = 10000;
    ToolExecutor::RunViaITool(blob, context);
    Require(blob.hasLastResult && blob.lastResult.success &&
            blob.lastResult.regions.size() == 1,
        "circle definition domain did not exclude bounding-box corner blobs");
    double blobCandidateCount = -1.0;
    for (const auto& measurement : blob.lastResult.measurements)
        if (measurement.name == "blobCandidateCount")
            blobCandidateCount = measurement.value;
    Require(std::abs(blobCandidateCount - 1.0) < 0.001,
        "inverted threshold generated foreground outside the circle domain");

    ROI polygon;
    polygon.type = ROI_TYPE_POLYGON;
    polygon.points = {{15.0f, 15.0f}, {95.0f, 20.0f}, {35.0f, 100.0f}};
    polygon.start = ImVec2(15.0f, 15.0f);
    polygon.end = ImVec2(95.0f, 100.0f);
    cv::Mat polygonCrop;
    RotatedROI::Transform polygonTransform;
    Require(RotatedROI::Extract(source, polygon, polygonCrop, polygonTransform),
        "polygon ROI domain extraction failed");
    Require(polygonTransform.domainMask.at<uchar>(0, 79) == 0 &&
            polygon.Contains(ImVec2(35.0f, 40.0f)) &&
            !polygon.Contains(ImVec2(90.0f, 90.0f)),
        "polygon ROI exact domain regressed");
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
    const std::uint64_t initialRevision = tool.parameterRevision;
    int roiIndex = ToolAssetService::BeginROICapture(tool, ToolAssetKind::TemplateMatch);
    Require(ROIState::IsValidIndex(roiIndex), "asset capture did not create an editable ROI");
    ROI roi = *ROIState::At(roiIndex);
    roi.start = ImVec2(10.0f, 12.0f);
    roi.end = ImVec2(30.0f, 32.0f);
    ROIState::Update(roiIndex, roi);

    ToolAssetCaptureResult capture =
        ToolAssetService::ConfirmROICapture(tool, ToolAssetKind::TemplateMatch);
    Require(capture.success && tool.templateImg.size() == cv::Size(20, 20) &&
        tool.hasTemplateROI && ROIState::ReadOnlyItems().empty(),
        "template asset capture workflow regressed");
    Require(tool.parameterRevision > initialRevision,
        "asset capture did not advance the tool parameter revision");
    Require(tool.templateImg.at<cv::Vec3b>(0, 0) == image.at<cv::Vec3b>(12, 10),
        "template asset pixels were captured from the wrong coordinates");

    roiIndex = ToolAssetService::BeginROICapture(tool, ToolAssetKind::ShapeTemplate);
    roi = *ROIState::At(roiIndex);
    roi.start = ImVec2(20.0f, 15.0f);
    roi.end = ImVec2(45.0f, 40.0f);
    ROIState::Update(roiIndex, roi);
    ROI unrelated;
    unrelated.type = ROI_TYPE_RECT;
    unrelated.start = ImVec2(1.0f, 1.0f);
    unrelated.end = ImVec2(3.0f, 3.0f);
    ROIState::Insert(0, unrelated);
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
    const std::uint64_t initialRevision = tool.parameterRevision;
    int roiIndex = ToolROIService::BeginSearchROIEdit(tool);
    ROI roi = *ROIState::At(roiIndex);
    roi.start = ImVec2(11.0f, 13.0f);
    roi.end = ImVec2(51.0f, 43.0f);
    ROIState::Update(roiIndex, roi);

    ROI unrelated;
    unrelated.type = ROI_TYPE_RECT;
    unrelated.start = ImVec2(1.0f, 1.0f);
    unrelated.end = ImVec2(2.0f, 2.0f);
    ROIState::Insert(0, unrelated);

    const ToolROIEditResult result = ToolROIService::ConfirmSearchROIEdit(tool);
    Require(result.success && tool.searchROIs.size() == 1 &&
        tool.searchROIs[0].ToCvRect() == cv::Rect(11, 13, 40, 30),
        "bound ROI confirmation followed a stale vector index");
    Require(tool.parameterRevision > initialRevision,
        "bound ROI confirmation did not advance the tool parameter revision");
    Require(tool.useSearchROI && tool.yoloUseROI && tool.mcfUseROI &&
        tool.ocrUseROI && tool.qrUseROI && tool.mcfRoiW == 40,
        "bound ROI flags were not updated consistently");

    const ROI saved = tool.searchROIs[0];
    roiIndex = ToolROIService::BeginSearchROIEdit(tool);
    roi = *ROIState::At(roiIndex);
    roi.start = ImVec2(20.0f, 20.0f);
    ROIState::Update(roiIndex, roi);
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
    const ROI* runtimeMeasurementROI = ROIState::At(measurementIndex);
    Require(runtimeMeasurementROI != nullptr, "measurement runtime ROI was not recoverable by id");
    const std::vector<ROI> measurementBackup = measurement.searchROIs;
    ROI editedMeasurementROI = *runtimeMeasurementROI;
    editedMeasurementROI.end.x = 95.0f;
    ROIState::Update(measurementIndex, editedMeasurementROI);
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
    bool secondCameraDisconnected = false;
    auto secondCamera = std::make_unique<TestCameraAdapter>(&secondCameraDisconnected);
    TestCameraAdapter* secondCameraView = secondCamera.get();
    Require(HardwareAdapterService::RegisterCamera("line-b", std::move(secondCamera)) &&
        HardwareAdapterService::Camera("line-b") == secondCameraView &&
        HardwareAdapterService::CameraKeys() ==
            std::vector<std::string>({"default", "line-b"}),
        "multi-camera registration or enumeration failed");
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
    Require(cameraDisconnected && cameraView->stopped && secondCameraDisconnected &&
        plcDisconnected,
        "hardware adapters were not disconnected as a group");
    HardwareAdapterService::Clear();
    Require(HardwareAdapterService::Camera() == nullptr &&
        HardwareAdapterService::CameraKeys().empty() &&
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
    captureConfig.autoReconnect = true;
    captureConfig.reconnectFailureThreshold = 1;
    captureConfig.reconnectInitialDelayMs = 1;
    captureConfig.reconnectMaxDelayMs = 4;
    captureConfig.trigger.mode = CameraTriggerMode::Software;
    captureConfig.trigger.delayMicroseconds = 25.0;
    captureConfig.bufferPolicy = CameraBufferPolicy::LatestFrame;
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
    Require(cameraView->triggerConfigureCount >= 1 &&
            cameraView->softwareTriggerCount >= 1 &&
            cameraView->bufferPolicyConfigureCount >= 1 &&
            cameraView->bufferPolicy == CameraBufferPolicy::LatestFrame &&
            cameraSnapshot.cameraTrigger.mode == CameraTriggerMode::Software,
        "camera trigger or frame buffer configuration was not applied before capture");

    const int reconnectStartFrame = cameraSnapshot.cameraFrameIndex;
    cameraView->failGrabsRemaining = 1;
    HardwareRuntimeService::RequestCameraFrame();
    bool cameraRecovered = false;
    for (int attempt = 0; attempt < 400; ++attempt)
    {
        HardwareRuntimeService::Tick();
        const HardwareRuntimeSnapshot reconnectSnapshot = HardwareRuntimeService::Snapshot();
        if (reconnectSnapshot.cameraReconnectAttempts > 0 &&
            reconnectSnapshot.cameraFrameIndex > reconnectStartFrame)
        {
            cameraRecovered = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Require(cameraRecovered && cameraView->connectCount >= 2 && cameraView->startCount >= 2 &&
            cameraView->triggerConfigureCount >= 2 &&
            cameraView->bufferPolicyConfigureCount >= 2,
        "industrial camera did not recover and restore acquisition configuration after failure");

    ToolController::Reset();
    ToolChainState::ClearTools();
    ToolInstance cameraInputTool;
    cameraInputTool.type = 12;
    ToolChainState::AddTool(std::move(cameraInputTool));
    const int firstFrameIndex = cameraSnapshot.cameraFrameIndex;
    ToolController::RequestRunAll(true);
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

    const int linkedFrameIndex = HardwareRuntimeService::Snapshot().cameraFrameIndex;
    ToolController::Tick();
    bool linkedLoopContinued = false;
    for (int attempt = 0; attempt < 200; ++attempt)
    {
        HardwareRuntimeService::Tick();
        if (HardwareRuntimeService::Snapshot().cameraFrameIndex > linkedFrameIndex &&
            ToolController::GetMode() == ToolController::Mode::Running)
        {
            linkedLoopContinued = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Require(linkedLoopContinued,
        "camera-triggered inspection loop did not request the next frame");

    ToolController::Tick();
    ToolController::Reset();
    for (int attempt = 0; attempt < 20; ++attempt)
    {
        HardwareRuntimeService::Tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Require(ToolController::GetMode() == ToolController::Mode::Idle &&
        !HardwareRuntimeService::Snapshot().cameraToolRunPending,
        "stopping the camera inspection loop left a pending tool run");
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

    const int connectsBeforeRetry = modbusView->connectCount;
    modbusView->failWritesRemaining = 1;
    Require(HardwareRuntimeService::EnqueueConfiguredStatus(ToolResultStatus::Pass).success &&
        HardwareRuntimeService::WaitForOutputIdle(3000) && modbusView->lastValue &&
        modbusView->connectCount > connectsBeforeRetry,
        "queued Modbus output did not reconnect and retry after a write failure");

    auto auxiliaryOne = std::make_unique<TestModbusAdapter>();
    auto auxiliaryTwo = std::make_unique<TestModbusAdapter>();
    TestModbusAdapter* auxiliaryOneView = auxiliaryOne.get();
    TestModbusAdapter* auxiliaryTwoView = auxiliaryTwo.get();
    Require(auxiliaryOneView->Connect({"127.0.0.1", 1502, {}}).success &&
        auxiliaryTwoView->Connect({"127.0.0.1", 2502, {}}).success &&
        HardwareAdapterService::Register("aux-output-1", std::move(auxiliaryOne)) &&
        HardwareAdapterService::Register("aux-output-2", std::move(auxiliaryTwo)),
        "auxiliary output adapter setup failed");
    HardwareOutputConnectionConfig auxiliaryConfigOne;
    auxiliaryConfigOne.enabled = true;
    auxiliaryConfigOne.autoPublish = true;
    auxiliaryConfigOne.binding.kind = HardwareOutputKind::ModbusCoil;
    auxiliaryConfigOne.binding.adapterKey = "aux-output-1";
    auxiliaryConfigOne.binding.address = 31;
    HardwareOutputConnectionConfig auxiliaryConfigTwo = auxiliaryConfigOne;
    auxiliaryConfigTwo.binding.adapterKey = "aux-output-2";
    auxiliaryConfigTwo.binding.address = 32;
    HardwareRuntimeService::ConfigureAuxiliaryOutputBinding(auxiliaryConfigOne);
    HardwareRuntimeService::ConfigureAuxiliaryOutputBinding(auxiliaryConfigTwo);
    Require(HardwareRuntimeService::EnqueueConfiguredStatus(ToolResultStatus::Pass).success &&
        HardwareRuntimeService::WaitForOutputIdle(3000) &&
        auxiliaryOneView->lastAddress == 31 && auxiliaryOneView->lastValue &&
        auxiliaryTwoView->lastAddress == 32 && auxiliaryTwoView->lastValue &&
        HardwareRuntimeService::AuxiliaryOutputSnapshots().size() == 2,
        "automatic inspection result was not broadcast to auxiliary outputs");

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
    tcpBinding.sendQrJson = true;
    HardwareRuntimeService::ConfigureOutputBinding(tcpBinding, true);
    Require(HardwareRuntimeService::PublishInspectionStatus(
        ToolResultStatus::Pass, tcpBinding, {"SN-001", "SN-002"}).success &&
        tcpTransportView->lastText ==
            "{\"result\":\"OK\",\"serial\":\"SN-001\",\"serials\":[\"SN-001\",\"SN-002\"]}\r\n" &&
        tcpTransportView->sendCount == 3,
        "TCP QR JSON output did not preserve and escape decoded serials");

    HardwareRuntimeService::Shutdown();
    Require(HardwareAdapterService::Camera() == nullptr &&
        HardwareAdapterService::Keys().empty(),
        "hardware runtime shutdown left registered devices behind");
    FrameSourceState::Clear();
    ImageState::Clear();
}

void TestModbusHandshakeTriggersIndependentTaskCameraRun()
{
    HardwareRuntimeService::Shutdown();
    ToolController::Reset();
    ToolChainState::ClearTools();
    ToolChainState::ReplaceTaskGroups({});
    ImageState::Clear();

    Require(ToolChainState::CreateTaskGroup("任务A") >= 0 &&
        ToolChainState::CreateTaskGroup("任务B") >= 0,
        "PLC handshake task setup failed");

    int capturedTaskA = -1;
    int taskBExecutions = 0;
    ToolInstance taskA;
    taskA.type = 2;
    taskA.groupName = "任务A";
    taskA.toolImpl = std::make_unique<TestInputCaptureTool>(&capturedTaskA);
    ToolChainState::Tools().push_back(std::move(taskA));
    ToolInstance taskB;
    taskB.type = 2;
    taskB.groupName = "任务B";
    taskB.toolImpl = std::make_unique<TestCountingTool>(&taskBExecutions);
    ToolChainState::Tools().push_back(std::move(taskB));
    ImageState::SetImage(cv::Mat(4, 6, CV_8UC1, cv::Scalar(3)));

    auto camera = std::make_unique<TestCameraAdapter>(nullptr);
    TestCameraAdapter* cameraView = camera.get();
    HardwareAdapterService::SetCamera(std::move(camera));
    Require(cameraView->Connect({"plc-camera", 0, {}}).success,
        "PLC handshake camera connect failed");
    HardwareCameraConnectionConfig cameraConfig;
    cameraConfig.sourceName = "plc-camera";
    cameraConfig.autoCapture = false;
    cameraConfig.grabTimeoutMs = 100;
    Require(HardwareRuntimeService::StartCameraCapture(cameraConfig).success,
        "PLC handshake camera worker failed to start");

    auto modbus = std::make_unique<TestModbusAdapter>();
    TestModbusAdapter* modbusView = modbus.get();
    Require(modbusView->Connect({"127.0.0.1", 502, "1"}).success &&
        HardwareAdapterService::Register("plc-handshake", std::move(modbus)),
        "PLC handshake Modbus adapter setup failed");

    HardwareOutputBinding binding;
    binding.kind = HardwareOutputKind::ModbusCoil;
    binding.adapterKey = "plc-handshake";
    binding.address = 13;
    HardwareRuntimeService::ConfigureOutputBinding(binding, false);

    HardwareHandshakeConfig handshake;
    handshake.enabled = true;
    handshake.pollIntervalMs = 10;
    handshake.acknowledgementTimeoutMs = 5000;
    handshake.inspectionTimeoutMs = 3000;
    handshake.heartbeatIntervalMs = 1000;
    handshake.mappings = {
        {true, HardwareIoSignal::Trigger, HardwareIoDirection::Input,
            10, false, 0, "任务A"},
        {true, HardwareIoSignal::Busy, HardwareIoDirection::Output,
            11, false, 0, {}},
        {true, HardwareIoSignal::Done, HardwareIoDirection::Output,
            12, false, 30, {}},
        {true, HardwareIoSignal::Ok, HardwareIoDirection::Output,
            13, false, 0, "任务A"},
        {true, HardwareIoSignal::Ng, HardwareIoDirection::Output,
            14, false, 0, "任务A"},
        {true, HardwareIoSignal::Ok, HardwareIoDirection::Output,
            18, false, 0, "任务B"},
        {true, HardwareIoSignal::Ng, HardwareIoDirection::Output,
            19, false, 0, "任务B"},
        {true, HardwareIoSignal::Error, HardwareIoDirection::Output,
            15, false, 0, {}},
        {true, HardwareIoSignal::Heartbeat, HardwareIoDirection::Output,
            16, false, 0, {}},
        {true, HardwareIoSignal::Acknowledge, HardwareIoDirection::Input,
            17, false, 0, {}}
    };
    HardwareRuntimeService::ConfigureModbusHandshake(handshake);

    const std::uint64_t ignoredBeforeSingleSlot =
        HardwareRuntimeService::Snapshot().handshakeIgnoredTriggerCount;
    const DeviceOperationResult acceptedSingleSlot =
        HardwareRuntimeService::RequestTaskInspection("任务A", false);
    const DeviceOperationResult rejectedSecondTask =
        HardwareRuntimeService::RequestTaskInspection("任务B", false);
    const DeviceOperationResult rejectedHandshakeTest =
        HardwareRuntimeService::RequestHandshakeTest(ToolResultStatus::Pass);
    Require(acceptedSingleSlot.success && !rejectedSecondTask.success &&
        !rejectedHandshakeTest.success &&
        HardwareRuntimeService::Snapshot().handshakeIgnoredTriggerCount ==
            ignoredBeforeSingleSlot + 1,
        "PLC handshake accepted more than one pending request");
    HardwareRuntimeService::ConfigureModbusHandshake(handshake);

    auto setCoil = [modbusView](std::uint16_t address, bool value)
    {
        std::lock_guard<std::mutex> lock(modbusView->ioMutex);
        modbusView->coilValues[address] = value;
    };
    auto hasWrite = [modbusView](std::uint16_t address, bool value)
    {
        std::lock_guard<std::mutex> lock(modbusView->ioMutex);
        return std::find(modbusView->writeHistory.begin(),
            modbusView->writeHistory.end(), std::make_pair(address, value)) !=
            modbusView->writeHistory.end();
    };

    setCoil(10, false);
    setCoil(17, false);
    for (int tick = 0; tick < 20; ++tick)
    {
        HardwareRuntimeService::Tick();
        ToolController::Tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    setCoil(10, true);

    bool awaitingAcknowledge = false;
    for (int tick = 0; tick < 1200; ++tick)
    {
        HardwareRuntimeService::Tick();
        ToolController::Tick();
        const HardwareRuntimeSnapshot snapshot = HardwareRuntimeService::Snapshot();
        if (snapshot.handshakeAwaitingAcknowledge)
        {
            awaitingAcknowledge = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    Require(awaitingAcknowledge && capturedTaskA == 17 &&
        taskBExecutions == 0 &&
        ToolController::GetLastRunTaskGroupName() == "任务A",
        "PLC Trigger did not capture a frame and run only its mapped task");
    for (int tick = 0; tick < 300 &&
        !(hasWrite(11, true) && hasWrite(11, false) &&
            hasWrite(12, true) && hasWrite(13, true)); ++tick)
    {
        HardwareRuntimeService::Tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    Require(hasWrite(11, true) && hasWrite(11, false) &&
        hasWrite(12, true) && hasWrite(13, true) &&
        !hasWrite(14, true) && !hasWrite(15, true) &&
        !hasWrite(18, true) && !hasWrite(19, true),
        "PLC handshake did not isolate task-specific OK/NG outputs");

    setCoil(17, true);
    bool acknowledged = false;
    for (int tick = 0; tick < 300; ++tick)
    {
        HardwareRuntimeService::Tick();
        ToolController::Tick();
        if (!HardwareRuntimeService::Snapshot().handshakeActive)
        {
            acknowledged = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    Require(acknowledged && hasWrite(13, false),
        "PLC ACK did not close the handshake and reset result outputs");

    HardwareRuntimeService::DisconnectCamera();
    const fs::path folder = fs::temp_directory_path() /
        ("imgui_opencv_plc_folder_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code folderError;
    fs::create_directories(folder, folderError);
    const fs::path firstFolderImage = folder / "01.png";
    const fs::path secondFolderImage = folder / "02.png";
    Require(!folderError &&
        cv::imwrite(firstFolderImage.string(),
            cv::Mat(8, 8, CV_8UC1, cv::Scalar(41))) &&
        cv::imwrite(secondFolderImage.string(),
            cv::Mat(8, 8, CV_8UC1, cv::Scalar(93))) &&
        ToolChainState::SetTaskGroupImageFolder(
            0, folder.string(), firstFolderImage.string(), 2),
        "PLC folder fallback setup failed");

    auto runFolderTrigger = [&](int expectedPixel, int expectedFolderIndex,
        int busyTriggerCount)
    {
        setCoil(10, false);
        setCoil(17, false);
        for (int tick = 0; tick < 30; ++tick)
        {
            HardwareRuntimeService::Tick();
            ToolController::Tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        {
            std::lock_guard<std::mutex> lock(modbusView->ioMutex);
            modbusView->writeHistory.clear();
        }
        capturedTaskA = -1;
        const std::uint64_t ignoredBefore =
            HardwareRuntimeService::Snapshot().handshakeIgnoredTriggerCount;
        setCoil(10, true);

        if (busyTriggerCount > 0)
        {
            for (int tick = 0; tick < 300; ++tick)
            {
                HardwareRuntimeService::Tick();
                ToolController::Tick();
                if (HardwareRuntimeService::Snapshot().handshakeActive)
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            for (int trigger = 0; trigger < busyTriggerCount; ++trigger)
            {
                setCoil(10, false);
                for (int tick = 0; tick < 12; ++tick)
                {
                    HardwareRuntimeService::Tick();
                    ToolController::Tick();
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                setCoil(10, true);
                for (int tick = 0; tick < 12; ++tick)
                {
                    HardwareRuntimeService::Tick();
                    ToolController::Tick();
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            }
        }

        bool folderRunAwaitingAck = false;
        for (int tick = 0; tick < 1200; ++tick)
        {
            HardwareRuntimeService::Tick();
            ToolController::Tick();
            if (HardwareRuntimeService::Snapshot().handshakeAwaitingAcknowledge)
            {
                folderRunAwaitingAck = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        for (int tick = 0; tick < 300 &&
            !(hasWrite(11, true) && hasWrite(11, false) &&
                hasWrite(12, true) && hasWrite(13, true)); ++tick)
        {
            HardwareRuntimeService::Tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        Require(folderRunAwaitingAck,
            "PLC folder Trigger did not reach the ACK phase");
        Require(capturedTaskA == expectedPixel && taskBExecutions == 0,
            "PLC folder Trigger used the wrong image or executed another task");
        Require(ToolChainState::ReadOnlyTaskGroups()[0].imageFolderIndex ==
                expectedFolderIndex,
            "PLC folder Trigger did not advance to the expected image index");
        Require(hasWrite(11, true) && hasWrite(11, false) &&
            hasWrite(12, true) && hasWrite(13, true) && !hasWrite(15, true),
            "PLC folder Trigger did not publish Busy/Done/OK outputs");
        Require(HardwareRuntimeService::Snapshot().handshakeIgnoredTriggerCount >=
                ignoredBefore + static_cast<std::uint64_t>(busyTriggerCount),
            "Busy-period PLC Trigger ignore count did not increase as expected");

        setCoil(17, true);
        bool folderRunAcknowledged = false;
        for (int tick = 0; tick < 300; ++tick)
        {
            HardwareRuntimeService::Tick();
            ToolController::Tick();
            if (!HardwareRuntimeService::Snapshot().handshakeActive)
            {
                folderRunAcknowledged = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        Require(folderRunAcknowledged,
            "PLC folder fallback result was not acknowledged");
        const std::uint64_t completedSerial =
            ToolController::GetCompletedBatchSerial();
        for (int tick = 0; tick < 100; ++tick)
        {
            HardwareRuntimeService::Tick();
            ToolController::Tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        Require(ToolController::GetCompletedBatchSerial() == completedSerial &&
            ToolChainState::ReadOnlyTaskGroups()[0].imageFolderIndex ==
                expectedFolderIndex,
            "Busy-period PLC Triggers were queued and replayed after ACK");
    };

    runFolderTrigger(41, 0, 5);
    runFolderTrigger(93, 1, 0);

    setCoil(10, false);
    setCoil(17, false);
    for (int tick = 0; tick < 20; ++tick)
    {
        HardwareRuntimeService::Tick();
        ToolController::Tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    handshake.acknowledgementTimeoutMs = 100;
    HardwareRuntimeService::ConfigureModbusHandshake(handshake);
    Require(HardwareRuntimeService::RequestHandshakeTest(
        ToolResultStatus::Fail).success,
        "full PLC handshake self-test request failed");
    bool selfTestAwaitingAck = false;
    for (int tick = 0; tick < 500; ++tick)
    {
        HardwareRuntimeService::Tick();
        if (HardwareRuntimeService::Snapshot().handshakeAwaitingAcknowledge)
        {
            selfTestAwaitingAck = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    for (int tick = 0; tick < 300 && !hasWrite(14, true); ++tick)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    Require(selfTestAwaitingAck && hasWrite(14, true),
        "full PLC handshake self-test did not publish the selected result");

    bool acknowledgeTimeoutObserved = false;
    for (int tick = 0; tick < 500; ++tick)
    {
        HardwareRuntimeService::Tick();
        const HardwareRuntimeSnapshot snapshot = HardwareRuntimeService::Snapshot();
        if (!snapshot.handshakeActive && snapshot.handshakeAlarm &&
            snapshot.handshakeAlarmMessage.find("确认应答超时") != std::string::npos)
        {
            acknowledgeTimeoutObserved = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    Require(acknowledgeTimeoutObserved &&
        HardwareRuntimeService::WaitForOutputIdle(1000),
        "PLC ACK timeout did not terminate the handshake cleanly");
    {
        std::lock_guard<std::mutex> lock(modbusView->ioMutex);
        Require(!modbusView->coilValues[11] && !modbusView->coilValues[12] &&
            !modbusView->coilValues[13] && !modbusView->coilValues[14] &&
            !modbusView->coilValues[15],
            "PLC ACK timeout left Busy/Done/result outputs active");
    }

    HardwareRuntimeService::Shutdown();
    ToolController::Reset();
    ToolChainState::ClearTools();
    ToolChainState::ReplaceTaskGroups({});
    ImageState::Clear();
    fs::remove_all(folder, folderError);
}

void TestFrameArchiveService()
{
    FrameArchiveService::Shutdown();
    const fs::path outputDirectory = fs::temp_directory_path() /
        ("imgui_opencv_frame_archive_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    FrameArchiveConfig config;
    config.enabled = true;
    config.directory = outputDirectory.string();
    config.format = FrameArchiveFormat::Png;
    config.saveEveryN = 2;
    config.maxQueue = 8;
    FrameArchiveService::Configure(config, false);

    const cv::Mat frame(12, 16, CV_8UC3, cv::Scalar(20, 80, 160));
    for (int index = 1; index <= 5; ++index)
        FrameArchiveService::Enqueue(frame, "test-camera", index, 1000.0 + index);

    Require(FrameArchiveService::WaitUntilIdle(3000),
        "frame archive worker did not become idle");
    const FrameArchiveSnapshot snapshot = FrameArchiveService::Snapshot();
    Require(snapshot.savedFrames == 3 && snapshot.failedFrames == 0 &&
        snapshot.droppedFrames == 0 && fs::exists(outputDirectory),
        "frame archive sampling or asynchronous save failed");

    std::size_t pngCount = 0;
    for (const fs::directory_entry& entry : fs::directory_iterator(outputDirectory))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".png")
            ++pngCount;
    }
    Require(pngCount == 3 && !snapshot.lastSavedPath.empty() &&
        !FrameArchiveService::SettingsPath().empty(),
        "frame archive output paths regressed");

    config.enabled = false;
    FrameArchiveService::Configure(config, false);
    FrameArchiveService::Enqueue(frame, "test-camera", 6, 1006.0);
    Require(FrameArchiveService::WaitUntilIdle(1000) &&
        FrameArchiveService::Snapshot().savedFrames == 3,
        "disabled frame archive still saved images");

    FrameArchiveService::Shutdown();
    fs::remove_all(outputDirectory);
}

void TestRecipeAutosaveService()
{
    RecipeAutosaveService::Shutdown();
    const fs::path directory = fs::temp_directory_path() /
        ("imgui_opencv_recipe_autosave_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path recipePath = directory / "autosave.recipe";
    const fs::path assetPath = directory / "mcf_reference.png";

    RecipeData first;
    first.name = "autosave-first";
    RecipeToolInstance firstTool;
    ToolInstance source;
    source.type = 10;
    source.label = "finder-a";
    source.mcfRefImage = cv::Mat(10, 12, CV_8UC3, cv::Scalar(10, 20, 30));
    firstTool.CaptureFrom(source, true);
    firstTool.multiColorReferenceFile = "mcf_reference.png";
    first.tools.push_back(std::move(firstTool));

    RecipeAutosaveService::ConfigureTarget(recipePath.string());
    RecipeAutosaveService::MarkDirty(RecipeDirtyKind::All);
    Require(RecipeAutosaveService::ShouldCapture(false),
        "recipe autosave did not become capture-ready");
    RecipeAutosaveService::Submit(std::move(first));
    Require(RecipeAutosaveService::WaitUntilIdle(5000) &&
        fs::exists(recipePath) && fs::exists(assetPath),
        "recipe autosave did not atomically write recipe assets");
    const fs::file_time_type assetWriteTime = fs::last_write_time(assetPath);

    RecipeData second;
    second.name = "autosave-second";
    RecipeToolInstance secondTool;
    source.label = "finder-b";
    secondTool.CaptureFrom(source, false);
    secondTool.multiColorReferenceFile = "mcf_reference.png";
    second.tools.push_back(std::move(secondTool));
    RecipeAutosaveService::MarkDirty(RecipeDirtyKind::Parameters);
    RecipeAutosaveService::Submit(std::move(second));
    Require(RecipeAutosaveService::WaitUntilIdle(5000),
        "parameter-only recipe autosave did not finish");
    Require(fs::last_write_time(assetPath) == assetWriteTime,
        "parameter-only autosave rewrote an unchanged reference asset");

    const RecipeAutosaveSnapshot snapshot = RecipeAutosaveService::Snapshot();
    Require(snapshot.completedSaveCount == 2 && snapshot.failedSaveCount == 0 &&
        !snapshot.lastSavedAt.empty() && fs::exists(snapshot.backupPath),
        "recipe autosave status or backup rotation regressed");

    RecipeData loaded;
    Require(RecipeManager::Load(recipePath.string().c_str(), loaded) &&
        loaded.name == "autosave-second" && loaded.tools.size() == 1 &&
        !loaded.tools[0].multiColorReferenceImage.empty(),
        "autosaved recipe or external multi-color asset did not load");

    std::string restoreError;
    Require(RecipeAutosaveService::RestoreBackup(&restoreError) && restoreError.empty() &&
        RecipeManager::Load(recipePath.string().c_str(), loaded) &&
        loaded.name == "autosave-first",
        "recipe backup restore failed");

    RecipeAutosaveService::Shutdown();
    fs::remove_all(directory);
}

void TestHardwareSettingsPersistence()
{
    const fs::path settingsPath = fs::temp_directory_path() /
        ("imgui_opencv_hardware_settings_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");

    HardwarePanelSettings source;
    source.cameraAddress = "rtsp://192.168.10.20/live";
    source.cameraSourceName = "line-a-camera";
    source.cameraBackend = 6;
    source.cameraOrientation = 3;
    source.cameraTimeoutMs = 880;
    source.cameraIntervalMs = 75;
    source.cameraAutoCapture = false;
    source.cameraRunAfterCapture = false;
    source.cameraTriggerBeforeRun = false;
    source.cameraAutoExposure = false;
    source.cameraExposure = 10000.0f;
    source.cameraGain = 12.5f;
    source.outputType = 3;
    source.outputKey = "quality-gate";
    source.outputAddress = "192.168.10.30";
    source.outputPort = 5000;
    source.outputResource = "cell-1";
    source.outputTarget = "ns=3;s=Result.OK";
    source.outputAddressValue = 125;
    source.outputTimeoutMs = 2300;
    source.plcHoldingRegister = true;
    source.tcpPassText = "OK-A";
    source.tcpFailText = "NG-A";
    source.tcpAppendCrLf = false;
    source.outputInvert = true;
    source.outputAutoPublish = true;
    source.outputHandshakeEnabled = true;
    source.outputPollIntervalMs = 35;
    source.outputAcknowledgementTimeoutMs = 2400;
    source.outputInspectionTimeoutMs = 18000;
    source.outputHeartbeatIntervalMs = 750;
    source.outputAutoReconnect = true;
    source.outputReconnectFailureThreshold = 4;
    source.outputReconnectInitialDelayMs = 180;
    source.outputReconnectMaxDelayMs = 4200;
    source.outputIoMappings = {
        {true, HardwareIoSignal::Trigger, HardwareIoDirection::Input,
            21, true, 0, "任务A"},
        {true, HardwareIoSignal::Done, HardwareIoDirection::Output,
            22, false, 350, {}}
    };
    source.auxiliaryOutputs.resize(kHardwareAuxiliaryOutputCount);
    source.auxiliaryOutputs[0].enabled = true;
    source.auxiliaryOutputs[0].adapterType = HardwareOutputAdapterType::TcpText;
    source.auxiliaryOutputs[0].binding.adapterKey = "output-aux-01";
    source.auxiliaryOutputs[0].endpoint.address = "192.168.10.41";
    source.auxiliaryOutputs[0].endpoint.port = 5101;
    source.auxiliaryOutputs[0].binding.passText = "PASS-AUX";
    source.auxiliaryOutputs[0].binding.failText = "FAIL-AUX";
    source.auxiliaryOutputs[0].autoPublish = true;
    source.auxiliaryOutputs[1].enabled = false;
    source.auxiliaryOutputs[1].binding.adapterKey = "output-aux-02";
    source.auxiliaryOutputs[2].enabled = false;
    source.auxiliaryOutputs[2].binding.adapterKey = "output-aux-03";

    std::string error;
    if (!HardwareSettingsService::Save(source, settingsPath.string(), &error))
        throw std::runtime_error("hardware settings were not saved: " + error);
    Require(error.empty() && fs::exists(settingsPath),
        "hardware settings save returned success without a target file");

    const HardwarePanelSettings loaded = HardwareSettingsService::Load(settingsPath.string());
    Require(loaded.cameraAddress == source.cameraAddress &&
        loaded.cameraSourceName == source.cameraSourceName &&
        loaded.cameraBackend == source.cameraBackend &&
        loaded.cameraOrientation == source.cameraOrientation &&
        loaded.cameraTimeoutMs == source.cameraTimeoutMs &&
        loaded.cameraIntervalMs == source.cameraIntervalMs &&
        loaded.cameraAutoCapture == source.cameraAutoCapture &&
        loaded.cameraRunAfterCapture == source.cameraRunAfterCapture &&
        loaded.cameraTriggerBeforeRun == source.cameraTriggerBeforeRun &&
        loaded.cameraAutoExposure == source.cameraAutoExposure &&
        std::abs(loaded.cameraExposure - source.cameraExposure) < 0.001f &&
        std::abs(loaded.cameraGain - source.cameraGain) < 0.001f &&
        loaded.cameras.size() == kHardwareCameraCount &&
        loaded.cameras[0].address == source.cameraAddress &&
        loaded.cameras[0].sourceName == source.cameraSourceName &&
        loaded.cameras[15].address == "15" &&
        loaded.cameras[15].sourceName == "camera-16" &&
        loaded.outputType == source.outputType &&
        loaded.outputKey == source.outputKey &&
        loaded.outputAddress == source.outputAddress &&
        loaded.outputPort == source.outputPort &&
        loaded.outputResource == source.outputResource &&
        loaded.outputTarget == source.outputTarget &&
        loaded.outputAddressValue == source.outputAddressValue &&
        loaded.outputTimeoutMs == source.outputTimeoutMs &&
        loaded.plcHoldingRegister == source.plcHoldingRegister &&
        loaded.tcpPassText == source.tcpPassText &&
        loaded.tcpFailText == source.tcpFailText &&
        loaded.tcpAppendCrLf == source.tcpAppendCrLf &&
        loaded.outputInvert == source.outputInvert &&
        loaded.outputAutoPublish == source.outputAutoPublish &&
        loaded.outputHandshakeEnabled == source.outputHandshakeEnabled &&
        loaded.outputPollIntervalMs == source.outputPollIntervalMs &&
        loaded.outputAcknowledgementTimeoutMs ==
            source.outputAcknowledgementTimeoutMs &&
        loaded.outputInspectionTimeoutMs == source.outputInspectionTimeoutMs &&
        loaded.outputHeartbeatIntervalMs == source.outputHeartbeatIntervalMs &&
        loaded.outputAutoReconnect == source.outputAutoReconnect &&
        loaded.outputReconnectFailureThreshold ==
            source.outputReconnectFailureThreshold &&
        loaded.outputReconnectInitialDelayMs ==
            source.outputReconnectInitialDelayMs &&
        loaded.outputReconnectMaxDelayMs == source.outputReconnectMaxDelayMs &&
        loaded.outputIoMappings.size() == 2 &&
        loaded.outputIoMappings[0].signal == HardwareIoSignal::Trigger &&
        loaded.outputIoMappings[0].direction == HardwareIoDirection::Input &&
        loaded.outputIoMappings[0].address == 21 &&
        loaded.outputIoMappings[0].invert &&
        loaded.outputIoMappings[0].taskGroupName == "任务A" &&
        loaded.outputIoMappings[1].pulseMs == 350 &&
        loaded.auxiliaryOutputs.size() == kHardwareAuxiliaryOutputCount &&
        loaded.auxiliaryOutputs[0].enabled &&
        loaded.auxiliaryOutputs[0].adapterType == HardwareOutputAdapterType::TcpText &&
        loaded.auxiliaryOutputs[0].binding.adapterKey == "output-aux-01" &&
        loaded.auxiliaryOutputs[0].endpoint.address == "192.168.10.41" &&
        loaded.auxiliaryOutputs[0].endpoint.port == 5101 &&
        loaded.auxiliaryOutputs[0].binding.passText == "PASS-AUX" &&
        !loaded.auxiliaryOutputs[1].enabled &&
        !HardwareSettingsService::SettingsPath().empty(),
        "hardware settings round trip lost camera or output fields");

    HardwarePanelSettings updated = loaded;
    updated.activeCameraIndex = 15;
    updated.cameras[15].address = "rtsp://192.168.10.35/camera16";
    updated.cameras[15].sourceName = "line-a-camera-16";
    updated.cameras[15].backend = 3;
    updated.cameras[15].exposure = -3.5f;
    updated.cameras[14].address = "192.168.20.22";
    updated.cameras[14].sourceName = "hikrobot-gige";
    updated.cameras[14].backend = 5;
    updated.cameras[14].exposure = 12500.0f;
    updated.cameras[14].triggerMode = 2;
    updated.cameras[14].triggerDelayMicroseconds = 275.0f;
    updated.cameras[14].bufferPolicy = 1;
    updated.cameras[14].ptpEnabled = true;
    updated.cameras[15].triggerMode = 1;
    updated.cameras[15].triggerDelayMicroseconds = 80.0f;
    Require(HardwareSettingsService::Save(updated, settingsPath.string(), &error) &&
        fs::exists(settingsPath.string() + ".bak"),
        "hardware settings backup rotation failed");
    const HardwarePanelSettings multiCameraLoaded =
        HardwareSettingsService::Load(settingsPath.string());
    Require(multiCameraLoaded.activeCameraIndex == 15 &&
        multiCameraLoaded.cameras.size() == kHardwareCameraCount &&
        multiCameraLoaded.cameras[15].address == updated.cameras[15].address &&
        multiCameraLoaded.cameras[15].sourceName == updated.cameras[15].sourceName &&
        multiCameraLoaded.cameras[15].backend == updated.cameras[15].backend &&
        std::abs(multiCameraLoaded.cameras[15].exposure -
            updated.cameras[15].exposure) < 0.001f &&
        multiCameraLoaded.cameras[14].address == "192.168.20.22" &&
        multiCameraLoaded.cameras[14].backend == 5 &&
        std::abs(multiCameraLoaded.cameras[14].exposure - 12500.0f) < 0.001f &&
        multiCameraLoaded.cameras[14].triggerMode == 2 &&
        std::abs(multiCameraLoaded.cameras[14].triggerDelayMicroseconds -
            275.0f) < 0.001f &&
        multiCameraLoaded.cameras[14].bufferPolicy == 1 &&
        multiCameraLoaded.cameras[14].ptpEnabled &&
        multiCameraLoaded.cameras[15].triggerMode == 1 &&
        std::abs(multiCameraLoaded.cameras[15].triggerDelayMicroseconds -
            80.0f) < 0.001f,
        "16-camera hardware settings were not persisted independently");
    {
        std::ofstream corrupt(settingsPath, std::ios::binary | std::ios::trunc);
        corrupt << "{";
    }
    const HardwarePanelSettings recovered =
        HardwareSettingsService::Load(settingsPath.string());
    Require(recovered.cameraAddress == source.cameraAddress,
        "invalid hardware settings did not fall back to last valid backup");
    Require(HardwareSettingsService::RestoreLastValid(
        settingsPath.string(), &error) && error.empty(),
        "explicit hardware settings backup restore failed");
    const HardwarePanelSettings restored =
        HardwareSettingsService::Load(settingsPath.string());
    Require(restored.cameraAddress == source.cameraAddress,
        "restored hardware settings do not match backup");

    fs::remove(settingsPath);
    fs::remove(settingsPath.string() + ".bak");
    fs::remove(settingsPath.string() + ".tmp");
}

void TestHardwareTaskTriggerMappingSynchronization()
{
    std::vector<std::string> taskNames;
    for (int index = 1; index <= 16; ++index)
    {
        char name[16] = {};
        std::snprintf(name, sizeof(name), "任务%02d", index);
        taskNames.emplace_back(name);
    }

    std::vector<HardwareIoMapping> mappings = HardwarePanelSettings{}.outputIoMappings;
    Require(HardwareSettingsService::EnsureTaskTriggerMappings(mappings, taskNames),
        "task Trigger synchronization did not add missing mappings");
    Require(mappings.size() == 53,
        "task IO synchronization did not produce 16 Trigger/OK/NG groups plus shared signals");

    for (int index = 0; index < 16; ++index)
    {
        const auto found = std::find_if(mappings.begin(), mappings.end(),
            [&](const HardwareIoMapping& mapping)
            {
                return mapping.signal == HardwareIoSignal::Trigger &&
                    mapping.taskGroupName == taskNames[index];
            });
        const std::uint16_t expectedAddress = static_cast<std::uint16_t>(
            index == 0 ? 0 : index + 7);
        Require(found != mappings.end() &&
            found->direction == HardwareIoDirection::Input &&
            found->address == expectedAddress,
            "task Trigger synchronization assigned an unexpected address");
        const auto ok = std::find_if(mappings.begin(), mappings.end(),
            [&taskNames, index](const HardwareIoMapping& mapping)
            {
                return mapping.signal == HardwareIoSignal::Ok &&
                    mapping.taskGroupName == taskNames[index];
            });
        const auto ng = std::find_if(mappings.begin(), mappings.end(),
            [&taskNames, index](const HardwareIoMapping& mapping)
            {
                return mapping.signal == HardwareIoSignal::Ng &&
                    mapping.taskGroupName == taskNames[index];
            });
        Require(ok != mappings.end() && ng != mappings.end(),
            "task IO synchronization did not add independent OK/NG outputs");
    }
    Require(!HardwareSettingsService::EnsureTaskTriggerMappings(mappings, taskNames),
        "task Trigger synchronization was not idempotent");

    const std::vector<HardwareIoMapping> standard =
        HardwareSettingsService::BuildStandardIoMappings(taskNames);
    Require(standard.size() == 53 &&
        standard[0].taskGroupName == "任务01" && standard[0].address == 0 &&
        standard[1].signal == HardwareIoSignal::Ok && standard[1].address == 3 &&
        standard[2].signal == HardwareIoSignal::Ng && standard[2].address == 4 &&
        standard[3].taskGroupName == "任务02" && standard[3].address == 8 &&
        standard[45].taskGroupName == "任务16" && standard[45].address == 22 &&
        standard[48].signal == HardwareIoSignal::Busy,
        "standard task Trigger/OK/NG mapping order or addresses regressed");

    std::vector<HardwareIoMapping> reconciled =
        HardwareSettingsService::BuildStandardIoMappings({"任务01", "任务02"});
    reconciled[0].address = 42;
    const std::vector<HardwareTaskIdentity> previous = {
        {101, "任务01"}, {102, "任务02"}
    };
    const std::vector<HardwareTaskIdentity> current = {
        {101, "工位A"}, {103, "任务03"}
    };
    Require(HardwareSettingsService::SynchronizeTaskTriggerMappings(
            reconciled, previous, current),
        "task Trigger synchronization did not reconcile rename/delete/add");
    const auto renamed = std::find_if(reconciled.begin(), reconciled.end(),
        [](const HardwareIoMapping& mapping)
        {
            return mapping.signal == HardwareIoSignal::Trigger &&
                mapping.taskGroupName == "工位A";
        });
    const auto added = std::find_if(reconciled.begin(), reconciled.end(),
        [](const HardwareIoMapping& mapping)
        {
            return mapping.signal == HardwareIoSignal::Trigger &&
                mapping.taskGroupName == "任务03";
        });
    const bool removedStillPresent = std::any_of(reconciled.begin(), reconciled.end(),
        [](const HardwareIoMapping& mapping)
        {
            return mapping.signal == HardwareIoSignal::Trigger &&
                mapping.taskGroupName == "任务02";
        });
    Require(renamed != reconciled.end() && renamed->address == 42 &&
        added != reconciled.end() && added->address == 8 &&
        !removedStillPresent,
        "task Trigger synchronization lost a custom address or kept a stale task");
    Require(!HardwareSettingsService::SynchronizeTaskTriggerMappings(
            reconciled, current, current),
        "task Trigger rename/delete synchronization was not idempotent");
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

    const CameraCapabilities capabilities = cameraView->Capabilities();
    Require(capabilities.softwareTrigger && !capabilities.hardwareTrigger &&
        capabilities.queueControl,
        "OpenCV camera capabilities were not reported accurately");
    CameraTriggerConfig trigger;
    trigger.mode = CameraTriggerMode::Software;
    Require(cameraView->ConfigureTrigger(trigger).success &&
        !cameraView->GrabFrame(frame, 10).success &&
        cameraView->ExecuteSoftwareTrigger().success,
        "OpenCV software trigger contract regressed");
    CameraFrameMetadata metadata;
    Require(cameraView->GrabFrame(frame, metadata, 10).success &&
        metadata.frameNumber == 1 && metadata.receivedTimestampNanoseconds > 0 &&
        metadata.exposureComplete && cameraView->Statistics().receivedFrames == 1,
        "OpenCV frame metadata or statistics contract regressed");
    trigger.mode = CameraTriggerMode::Continuous;
    Require(cameraView->ConfigureTrigger(trigger).success,
        "OpenCV continuous trigger mode could not be restored");

    Require(HardwareRuntimeService::GrabCameraFrame(250, "opencv-camera", 9, 88.0).success &&
        backendView->lastTimeoutMs == 250 &&
        FrameSourceState::HasFrame() &&
        FrameSourceState::Current().sourcePath == "opencv-camera" &&
        FrameSourceState::Current().frameIndex == 9 &&
        ImageState::Current().size() == cv::Size(7, 5),
        "OpenCV camera frame did not enter the normal FrameSource pipeline");

    HardwareRuntimeService::SetCameraOrientation(1);
    Require(HardwareRuntimeService::GrabCameraFrame(250, "opencv-camera-rotated", 10, 89.0).success &&
        FrameSourceState::Current().sourcePath == "opencv-camera-rotated" &&
        ImageState::Current().size() == cv::Size(5, 7),
        "camera orientation did not apply to the next frame without reconnecting");

    backendView->nextFrame.release();
    Require(!HardwareRuntimeService::GrabCameraFrame(100).success &&
        cameraView->ConnectionState() == DeviceConnectionState::Connected,
        "empty camera frame should fail without dropping the connection");
    cameraView->StopStream();
    Require(!cameraView->IsStreaming(), "OpenCV camera adapter stream state did not stop");
    HardwareRuntimeService::SetCameraOrientation(0);
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

    const cv::Size innerCorners(7, 5);
    constexpr int chessSquarePixels = 45;
    constexpr int chessBorderPixels = 40;
    const cv::Size chessboardSize(
        (innerCorners.width + 1) * chessSquarePixels + 2 * chessBorderPixels,
        (innerCorners.height + 1) * chessSquarePixels + 2 * chessBorderPixels);
    cv::Mat chessboard(chessboardSize, CV_8UC1, cv::Scalar(255));
    for (int row = 0; row <= innerCorners.height; ++row)
    {
        for (int column = 0; column <= innerCorners.width; ++column)
        {
            if ((row + column) % 2 != 0)
                continue;
            cv::rectangle(chessboard,
                cv::Rect(chessBorderPixels + column * chessSquarePixels,
                    chessBorderPixels + row * chessSquarePixels,
                    chessSquarePixels, chessSquarePixels),
                cv::Scalar(0), cv::FILLED);
        }
    }

    const std::vector<cv::Point2f> sourceQuad = {
        {0.0f, 0.0f},
        {static_cast<float>(chessboard.cols - 1), 0.0f},
        {static_cast<float>(chessboard.cols - 1),
            static_cast<float>(chessboard.rows - 1)},
        {0.0f, static_cast<float>(chessboard.rows - 1)}
    };
    const std::vector<std::vector<cv::Point2f>> destinationQuads = {
        {{80.0f, 60.0f}, {540.0f, 75.0f}, {520.0f, 410.0f}, {95.0f, 395.0f}},
        {{125.0f, 55.0f}, {565.0f, 125.0f}, {500.0f, 430.0f}, {70.0f, 350.0f}},
        {{60.0f, 120.0f}, {500.0f, 45.0f}, {570.0f, 360.0f}, {115.0f, 435.0f}}
    };
    std::vector<cv::Mat> chessboardImages;
    for (const auto& destinationQuad : destinationQuads)
    {
        cv::Mat image(480, 640, CV_8UC1, cv::Scalar(127));
        const cv::Mat transform = cv::getPerspectiveTransform(sourceQuad, destinationQuad);
        cv::warpPerspective(chessboard, image, transform, image.size(),
            cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(127));
        chessboardImages.push_back(std::move(image));
    }
    const CalibrationFitResult chessboardFit = CalibrationFitter::FitChessboard(
        chessboardImages, innerCorners, 10.0);
    Require(chessboardFit.success && chessboardFit.model.distortionEnabled &&
        chessboardFit.residuals.size() == chessboardImages.size() &&
        chessboardFit.totalImageCount == chessboardImages.size() &&
        chessboardFit.successfulImageCount == chessboardImages.size() &&
        std::isfinite(chessboardFit.meanError) &&
        std::isfinite(chessboardFit.model.fx) && chessboardFit.model.fx > 0.0 &&
        std::isfinite(chessboardFit.model.fy) && chessboardFit.model.fy > 0.0 &&
        std::isfinite(chessboardFit.rmsError) && chessboardFit.rmsError < 1.0,
        "synthetic chessboard calibration fit regressed");
    const auto reportPath = std::filesystem::temp_directory_path() /
        "imgui_opencv_calibration_acceptance.json";
    Require(CalibrationFitter::SaveAcceptanceReport(reportPath.string().c_str(),
        chessboardFit.model, chessboardFit.totalImageCount,
        chessboardFit.successfulImageCount, chessboardFit.residuals,
        chessboardFit.rmsError, chessboardFit.maxError, 1.0, 2.0),
        "calibration acceptance report export failed");
    std::ifstream reportFile(reportPath, std::ios::binary);
    nlohmann::json report;
    reportFile >> report;
    Require(report.value("acceptance", "") == "PASS" &&
        report.value("successfulImages", 0U) == chessboardImages.size() &&
        report.contains("calibration"),
        "calibration acceptance report content regressed");
    reportFile.close();
    std::filesystem::remove(reportPath);
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
    Require(gContext.image.data == ImageState::Current().data,
        "image state duplicated the compatibility context image");

    const ImmutableImageFrame immutableFrame = ImageState::AcquireImmutableFrame();
    Require(immutableFrame.valid() && immutableFrame.version == ImageState::Version(),
        "image state did not provide an immutable shared frame");

    const int sourceVersion = ImageState::Version();
    ImageState::SetDebugImage(cv::Mat(10, 14, CV_8UC3, cv::Scalar(21, 22, 23)));
    Require(ImageState::Version() == sourceVersion &&
        gContext.imageVersion == sourceVersion,
        "publishing a debug image incorrectly changed the source image version");
    Require(ImageState::Current().at<cv::Vec3b>(0, 0)[0] == 21 &&
        ImageState::Original().at<cv::Vec3b>(0, 0)[0] == 7,
        "debug image publication changed the original source image");

    ImageState::CurrentRef().setTo(cv::Scalar(11, 12, 13));
    Require(ImageState::Current().at<cv::Vec3b>(0, 0)[0] == 11,
        "image state mutable API did not update current image");
    Require(immutableFrame.current->at<cv::Vec3b>(0, 0)[0] == 7,
        "mutable image access changed an already acquired immutable frame");

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
    it.blob.minArea = 20;
    it.blob.maxArea = 200;
    it.showResultLabels = true;
    it.parametersDirty = true;

    ToolExecutor::Execute(it.type, it);
    Require(gContext.image.data == ImageState::Current().data,
        "tool executor did not share read-only blob input");
    Require(!gContext.unifiedResults.empty(), "tool executor did not publish result");
    Require(gContext.unifiedResults[0].regions.size() == 1, "tool executor blob result regressed");
    Require(!gContext.unifiedResults[0].regions[0].label.empty(),
        "common result label switch did not override legacy Blob label state");
    Require(!it.parametersDirty,
        "tool execution did not clear the pending parameter state");

    ToolInstance threshold;
    threshold.type = 3;
    threshold.threshold.useGray = false;
    threshold.threshold.enableThreshold = true;
    threshold.threshold.threshold = 100;
    ToolExecutor::Execute(threshold.type, threshold);
    Require(gContext.image.data == ImageState::Current().data,
        "published threshold result was duplicated between context and image state");
}

void TestToolExecutorDetachedExecutionPublishesOnCallerThread()
{
    cv::Mat input = cv::Mat::zeros(24, 24, CV_8UC1);
    cv::rectangle(input, cv::Rect(6, 6, 10, 10), cv::Scalar(220), cv::FILLED);
    ImageState::SetImage(input);
    ROIState::Clear();
    gContext.ClearUnifiedResults();

    ToolInstance target;
    target.type = 3;
    target.toolId = 91001;
    target.threshold.enableThreshold = true;
    target.threshold.threshold = 100;

    ToolInstance snapshot;
    VisionContext context;
    Require(ToolExecutor::PrepareDetached(target, ImageState::Current(), 2, snapshot, context),
        "detached tool preparation failed");
    Require(context.immutableImageOwner &&
        context.image.data == ImageState::Current().data,
        "detached preparation copied pixels instead of sharing an immutable frame");

    const cv::Mat currentBefore = ImageState::Current().clone();
    ToolExecutor::ToolExecutionOutput output;
    Require(ToolExecutor::ExecuteDetached(snapshot, context, 2, output) && output.completed,
        "detached threshold execution failed");
    Require(cv::norm(ImageState::Current(), currentBefore, cv::NORM_INF) == 0.0,
        "detached algorithm phase modified ImageState before publication");
    Require(gContext.unifiedResults.empty(),
        "detached algorithm phase published overlays from the worker phase");

    Require(ToolExecutor::PublishDetached(target, std::move(output)),
        "detached threshold publication did not report a debug image");
    Require(target.hasLastResult && target.lastResult.sourceToolId == target.toolId,
        "detached publication did not update the target tool result");
    Require(!gContext.unifiedResults.empty(),
        "detached publication did not publish the unified result");
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
    tools[2].resultRoiSecondSourceTool = 1;
    tools[2].resultRoiSecondSourceToolId = 5001;
    tools[2].fixture.sourceToolIndex = 1;
    tools[2].fixture.sourceToolId = 5001;
    ToolChainState::SetActiveIndex(2);
    ToolChainState::SetYoloLiveInstanceIndex(2);
    ToolChainState::SetYoloLiveDetect(true);

    Require(ToolChainState::MoveTool(2, 1),
        "tool chain move up was rejected");
    Require(tools[1].type == 7 && tools[2].type == 5, "tool chain move order regressed");
    Require(tools[1].resultRoiSourceTool == 2 &&
        tools[1].resultRoiSecondSourceTool == 2 &&
        tools[1].fixture.sourceToolIndex == 2,
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
    Require(tools[0].resultRoiSourceTool == 1 &&
        tools[0].resultRoiSecondSourceTool == 1 &&
        tools[0].fixture.sourceToolIndex == 1,
        "tool chain original deletion did not remap dependencies");
    Require(ToolChainState::ActiveIndex() == 0 &&
        ToolChainState::YoloLiveInstanceIndex() == 0 &&
        ToolChainState::YoloLiveDetect(),
        "tool chain original deletion did not remap selected/live index");

    bool destroyed = false;
    tools[0].toolImpl = std::make_unique<TestDisposableTool>(&destroyed);
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
    tools[0].type = 2;
    tools[1].type = 0;

    tools[1].resultRoiMode = static_cast<int>(ResultROIMode::NthResult);
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

    std::vector<ToolInstance> pairTools(3);
    pairTools[0].type = 1;
    pairTools[0].toolId = 301;
    pairTools[1].type = 2;
    pairTools[1].toolId = 302;
    pairTools[2].type = 15;
    pairTools[2].toolId = 303;
    pairTools[2].resultRoiMode = static_cast<int>(ResultROIMode::SelectedPair);
    pairTools[2].resultRoiSourceToolId = 301;
    pairTools[2].resultRoiSecondSourceToolId = 302;
    const std::vector<ToolChainDependency> pairDependencies =
        ToolChainValidator::DescribeDependencies(pairTools);
    Require(pairDependencies.size() == 2 && pairDependencies[0].valid &&
        pairDependencies[1].valid && pairDependencies[0].sourceIndex == 0 &&
        pairDependencies[1].sourceIndex == 1,
        "selected result pair dependencies were not both validated");
    pairTools[2].measureMode = 2;
    Require(!ToolChainValidator::Validate(pairTools).valid(),
        "line-line measurement accepted upstream tools without line results");
    pairTools[0].type = 7;
    pairTools[1].type = 7;
    Require(ToolChainValidator::Validate(pairTools).valid(),
        "line-line measurement rejected two line-result upstream tools");

    tools[1].resultRoiMode = static_cast<int>(ResultROIMode::Disabled);
    Require(ToolChainValidator::DescribeDependencies(tools).empty(),
        "disabled result ROI retained a stale dependency");
    tools[1].resultRoiMode = static_cast<int>(ResultROIMode::NthResult);

    tools[0].resultRoiSourceTool = 1;
    tools[0].resultRoiSourceToolId = tools[1].toolId;
    tools[0].resultRoiMode = static_cast<int>(ResultROIMode::NthResult);
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

    tools[0].resultRoiMode = static_cast<int>(ResultROIMode::Disabled);
    tools[0].resultRoiSourceTool = -1;
    tools[0].resultRoiSourceToolId = 0;
    tools[0].type = 0;
    tools[1].resultRoiMode = static_cast<int>(ResultROIMode::NthResult);
    tools[1].resultRoiSourceTool = 0;
    tools[1].resultRoiSourceToolId = tools[0].toolId;
    const ToolChainValidationResult incompatible = ToolChainValidator::Validate(tools);
    Require(!incompatible.valid() && !incompatible.issues.empty(),
        "non-spatial tool was accepted as a result ROI source");

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

    ToolInstance roiTool;
    roiTool.type = 0;
    roiTool.useSearchROI = true;
    tools.assign(1, roiTool);
    Require(!ToolChainPreflight::Check(tools, true, 1).valid(),
        "preflight accepted an unbound tool by borrowing a canvas ROI");
    ROI boundROI;
    boundROI.type = ROI_TYPE_RECT;
    boundROI.start = {2.0f, 3.0f};
    boundROI.end = {12.0f, 13.0f};
    tools[0].searchROIs.push_back(boundROI);
    Require(ToolChainPreflight::Check(tools, true, 1).valid(),
        "preflight rejected an explicitly bound search ROI");
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

    const auto utf8Path = [](const std::string& value)
    {
        return std::filesystem::path(std::u8string(value.begin(), value.end()));
    };
    const int taskExampleCounts[] = {2, 4, 6, 8, 10, 12, 16};
    for (int expectedCount : taskExampleCounts)
    {
        const std::string folderName = expectedCount < 10
            ? "0" + std::to_string(expectedCount) + "_tasks"
            : std::to_string(expectedCount) + "_tasks";
        const std::filesystem::path recipePath = recipeDir / "task_series" /
            folderName / (folderName + ".recipe");

        RecipeData data;
        Require(RecipeManager::Load(recipePath.string().c_str(), data),
            "multi-task example recipe failed to load");
        Require(data.taskGroups.size() == static_cast<size_t>(expectedCount),
            "multi-task example task count is incorrect");
        Require(data.tools.size() == static_cast<size_t>(expectedCount),
            "multi-task example tool count is incorrect");

        RecipeManager::Apply(data);
        std::string imagePath;
        Require(FrameNavigation::ConsumePendingImagePath(imagePath) &&
            std::filesystem::exists(utf8Path(imagePath)),
            "multi-task example default image was not resolved");

        const auto& groups = ToolChainState::ReadOnlyTaskGroups();
        const auto& tools = ToolChainState::ReadOnlyTools();
        Require(groups.size() == static_cast<size_t>(expectedCount) &&
            tools.size() == static_cast<size_t>(expectedCount),
            "multi-task example runtime state lost tasks or tools");

        for (const TaskGroupDefinition& group : groups)
        {
            Require(!group.name.empty() && !group.imagePath.empty() &&
                std::filesystem::exists(utf8Path(group.imagePath)),
                "multi-task example task image was not resolved");
            int matchingTools = 0;
            for (const ToolInstance& tool : tools)
            {
                if (tool.groupName == group.name)
                    ++matchingTools;
            }
            Require(matchingTools == 1,
                "multi-task example does not bind exactly one tool per task");
        }

        for (const ToolInstance& tool : tools)
        {
            Require(tool.toolId != 0 && !tool.label.empty() && !tool.groupName.empty(),
                "multi-task example tool metadata is incomplete");
            if (tool.type == 15)
            {
                Require(tool.useSearchROI && !tool.searchROIs.empty(),
                    "measurement example did not retain its explicit ROI");
            }
            else if (tool.type == 13)
            {
                Require(std::filesystem::exists(utf8Path(tool.ocrDetModelPath)) &&
                    std::filesystem::exists(utf8Path(tool.ocrRecModelPath)) &&
                    std::filesystem::exists(utf8Path(tool.ocrDictionaryPath)),
                    "multi-task OCR example model paths were not resolved");
                Require(!tool.useSearchROI && tool.searchROIs.empty(),
                    "full-image OCR example unexpectedly bound a search ROI");
            }
            else
            {
                Require(!tool.useSearchROI && tool.searchROIs.empty(),
                    "full-image example unexpectedly bound a search ROI");
            }
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

void TestOriginalImageToolPublishesResult()
{
    ToolController::Reset();
    ToolChainState::ClearTools();
    ImageState::SetImage(cv::Mat(8, 8, CV_8UC1, cv::Scalar(10)));

    ToolInstance originalTool;
    originalTool.type = 12;
    ToolChainState::Tools().push_back(std::move(originalTool));
    ToolController::RequestRunAll(false);
    for (int tick = 0;
        tick < 4 && ToolController::GetMode() != ToolController::Mode::Idle; ++tick)
    {
        ToolController::Tick();
    }

    Require(ToolController::GetMode() == ToolController::Mode::Idle &&
        ToolChainState::ReadOnlyTools()[0].hasLastResult &&
        ToolChainState::ReadOnlyTools()[0].lastResult.success &&
        ToolChainState::ReadOnlyTools()[0].lastResult.status == ToolResultStatus::Pass,
        "original-image tool completed without publishing a Pass result");

    ToolChainState::ClearTools();
    ToolController::OnToolChainChanged();
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
    first.toolImpl = std::make_unique<TestInputCaptureTool>(&firstSeen, 40);
    ToolChainState::Tools().push_back(std::move(first));

    ToolInstance second;
    second.type = 2;
    second.inputSourceMode = 1;
    second.toolImpl = std::make_unique<TestInputCaptureTool>(&previousOutputSeen);
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
    Require(HardwareRuntimeService::WaitForOutputIdle(3000) &&
        batchOutputView->lastAddress == 31 && batchOutputView->lastValue,
        "completed tool batch did not publish the aggregate Pass status");

    ImageState::Clear();
    batchOutputView->lastValue = true;
    ToolController::RequestRunAll(false);
    Require(HardwareRuntimeService::WaitForOutputIdle(3000) && !batchOutputView->lastValue,
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
    standaloneOriginal.toolImpl = std::make_unique<TestInputCaptureTool>(&standaloneOriginalSeen);
    ToolChainState::Tools().push_back(std::move(standaloneOriginal));
    ToolController::RequestRun(0);
    ToolController::Tick();
    Require(standaloneOriginalSeen == 10,
        "standalone original-tool input did not fall back to ImageState original");

    ImageState::SetDebugImage(cv::Mat(8, 8, CV_8UC1, cv::Scalar(70)));
    int standaloneProcessedSeen = -1;
    ToolChainState::Tools()[0].inputSourceMode = 1;
    ToolChainState::Tools()[0].toolImpl =
        std::make_unique<TestInputCaptureTool>(&standaloneProcessedSeen);
    ToolController::RequestRun(0);
    ToolController::Tick();
    Require(standaloneProcessedSeen == 70,
        "standalone processed-image input regressed");

    int deduplicatedExecutions = 0;
    ToolChainState::Tools()[0].toolImpl =
        std::make_unique<TestCountingTool>(&deduplicatedExecutions);
    ToolController::RequestRun(0);
    ToolController::RequestRun(0);
    ToolController::RequestRun(0);
    ToolController::Tick();
    ToolController::Tick();
    Require(deduplicatedExecutions == 1,
        "duplicate standalone requests for the same tool were not coalesced");

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
    ToolChainState::Tools()[0].toolImpl =
        std::make_unique<TestInputCaptureTool>(&disabledSeen);
    ToolController::RequestRun(0);
    ToolController::Tick();
    Require(disabledSeen == -1 && ToolChainState::Tools()[0].hasLastResult &&
        ToolChainState::Tools()[0].lastResult.skipped,
        "disabled tool was executed instead of producing a skipped result");

    ToolChainState::ClearTools();
    ToolController::OnToolChainChanged();
}

void TestToolControllerRunsOnlySelectedTaskGroupInOrder()
{
    ToolController::Reset();
    ToolChainState::ClearTools();
    ToolChainState::ReplaceTaskGroups({});
    ImageState::SetImage(cv::Mat(8, 8, CV_8UC1, cv::Scalar(20)));

    Require(ToolChainState::CreateTaskGroup("任务A") >= 0 &&
        ToolChainState::CreateTaskGroup("任务B") >= 0,
        "task-group execution test setup failed");

    int taskBExecutions = 0;
    std::vector<int> executionOrder;
    auto addTool = [&executionOrder](const char* groupName, int* executions, int marker)
    {
        ToolInstance tool;
        tool.type = 2;
        tool.groupName = groupName;
        if (marker > 0)
            tool.toolImpl = std::make_unique<TestOrderedTool>(&executionOrder, marker);
        else
            tool.toolImpl = std::make_unique<TestCountingTool>(executions);
        ToolChainState::Tools().push_back(std::move(tool));
    };
    addTool("任务A", nullptr, 1);
    addTool("任务B", &taskBExecutions, 0);
    addTool("任务A", nullptr, 2);

    ToolController::RequestRunTaskGroup("任务A", false, false);
    Require(ToolController::GetRunProgressTotal() == 2 &&
        ToolController::GetRunProgressCurrent() == 1,
        "task-group execution progress did not use the filtered tool count");
    for (int tick = 0;
        tick < 8 && ToolController::GetMode() != ToolController::Mode::Idle; ++tick)
    {
        ToolController::Tick();
    }

    Require(ToolController::GetMode() == ToolController::Mode::Idle &&
        executionOrder == std::vector<int>({1, 2}) && taskBExecutions == 0,
        "task-group execution did not skip tools from other tasks");
    Require(ToolChainState::ReadOnlyTools()[0].hasLastResult &&
        !ToolChainState::ReadOnlyTools()[1].hasLastResult &&
        ToolChainState::ReadOnlyTools()[2].hasLastResult,
        "task-group execution published results outside the selected task");
    Require(ToolController::WasLastRunTaskGroup() &&
        ToolController::GetLastRunTaskGroupName() == "任务A",
        "task-group execution scope was not retained for rerun");

    ToolController::RequestRepeatLastRun(false, false);
    for (int tick = 0;
        tick < 8 && ToolController::GetMode() != ToolController::Mode::Idle; ++tick)
    {
        ToolController::Tick();
    }
    Require(executionOrder == std::vector<int>({1, 2, 1, 2}) && taskBExecutions == 0,
        "rerun did not preserve the selected task scope");

    Require(ToolChainState::SetTaskGroupEnabled(1, false),
        "task-group execution test could not disable task B");
    executionOrder.clear();
    ToolController::RequestRunAll(false, false);
    Require(ToolController::GetRunProgressTotal() == 2,
        "run-all progress still counted a disabled task");
    for (int tick = 0;
        tick < 8 && ToolController::GetMode() != ToolController::Mode::Idle; ++tick)
    {
        ToolController::Tick();
    }
    Require(executionOrder == std::vector<int>({1, 2}) && taskBExecutions == 0 &&
        !ToolChainState::ReadOnlyTools()[1].hasLastResult,
        "run-all executed or published a disabled task");

    ToolController::Reset();
    ToolChainState::ClearTools();
    ToolChainState::ReplaceTaskGroups({});
}

void TestToolControllerStepsOnlySelectedTaskGroupInOrder()
{
    ToolController::Reset();
    ToolChainState::ClearTools();
    ToolChainState::ReplaceTaskGroups({});
    ImageState::SetImage(cv::Mat(8, 8, CV_8UC1, cv::Scalar(20)));

    Require(ToolChainState::CreateTaskGroup("任务A") >= 0 &&
        ToolChainState::CreateTaskGroup("任务B") >= 0,
        "task-group step test setup failed");

    int taskBExecutions = 0;
    std::vector<int> executionOrder;
    auto addTool = [&executionOrder](const char* groupName, int* executions, int marker)
    {
        ToolInstance tool;
        tool.type = 2;
        tool.groupName = groupName;
        if (marker > 0)
            tool.toolImpl = std::make_unique<TestOrderedTool>(&executionOrder, marker);
        else
            tool.toolImpl = std::make_unique<TestCountingTool>(executions);
        ToolChainState::Tools().push_back(std::move(tool));
    };
    addTool("任务A", nullptr, 1);
    addTool("任务B", &taskBExecutions, 0);
    addTool("任务A", nullptr, 2);

    ToolController::RequestStepNextTaskGroup("任务A");
    Require(ToolController::GetStepTotal() == 2 &&
        ToolController::GetCurrentIndex() == 0,
        "task-group step did not start at the first matching tool");
    ToolController::Tick();
    Require(ToolController::GetStepCursor() == 1 &&
        ToolController::GetStepToolIndex() == 0 &&
        executionOrder == std::vector<int>({1}) && taskBExecutions == 0,
        "first task-group step executed the wrong tool");

    ToolController::RequestStepNextTaskGroup("任务A");
    Require(ToolController::GetCurrentIndex() == 2,
        "task-group step used the global tool index as its cursor");
    ToolController::Tick();
    Require(ToolController::GetStepCursor() == 2 &&
        ToolController::GetStepToolIndex() == 2 &&
        executionOrder == std::vector<int>({1, 2}) && taskBExecutions == 0,
        "task-group step did not preserve filtered tool order");

    ToolController::Reset();
    ToolChainState::ClearTools();
    ToolChainState::ReplaceTaskGroups({});
}

void TestToolControllerUsesIndependentTaskImages()
{
    ToolController::Reset();
    ToolChainState::ClearTools();
    ToolChainState::ReplaceTaskGroups({});
    ImageState::SetImage(cv::Mat(8, 8, CV_8UC1, cv::Scalar(9)));

    const fs::path suffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const fs::path taskAPath = fs::temp_directory_path() /
        ("imgui_opencv_task_a_" + suffix.string() + ".png");
    const fs::path taskBPath = fs::temp_directory_path() /
        ("imgui_opencv_task_b_" + suffix.string() + ".png");
    Require(cv::imwrite(taskAPath.string(), cv::Mat(8, 8, CV_8UC1, cv::Scalar(23))) &&
        cv::imwrite(taskBPath.string(), cv::Mat(8, 8, CV_8UC1, cv::Scalar(187))),
        "independent task image test could not create input images");

    Require(ToolChainState::CreateTaskGroup("任务A") >= 0 &&
        ToolChainState::CreateTaskGroup("任务B") >= 0 &&
        ToolChainState::SetTaskGroupImagePath(0, taskAPath.string()) &&
        ToolChainState::SetTaskGroupImagePath(1, taskBPath.string()),
        "independent task image test setup failed");

    int taskACaptured = -1;
    int taskBCaptured = -1;
    std::vector<int> executionOrder;
    auto addTool = [&executionOrder](const char* groupName, int marker, int* captured)
    {
        ToolInstance orderTool;
        orderTool.type = 2;
        orderTool.groupName = groupName;
        orderTool.toolImpl = std::make_unique<TestOrderedTool>(&executionOrder, marker);
        ToolChainState::Tools().push_back(std::move(orderTool));

        ToolInstance captureTool;
        captureTool.type = 2;
        captureTool.groupName = groupName;
        captureTool.toolImpl = std::make_unique<TestInputCaptureTool>(captured);
        ToolChainState::Tools().push_back(std::move(captureTool));
    };
    addTool("任务B", 2, &taskBCaptured);
    addTool("任务A", 1, &taskACaptured);

    ToolController::RequestRunAll(false, false);
    for (int tick = 0;
        tick < 16 && ToolController::GetMode() != ToolController::Mode::Idle; ++tick)
    {
        ToolController::Tick();
    }

    Require(ToolController::GetMode() == ToolController::Mode::Idle &&
        executionOrder == std::vector<int>({1, 2}),
        "run-all did not follow task-group order for independent images");
    Require(taskACaptured == 23 && taskBCaptured == 187,
        "task tools did not receive their independently assigned images");
    Require(!ToolController::GetTaskResultImage("任务A").empty() &&
        !ToolController::GetTaskResultImage("任务B").empty() &&
        ToolController::GetTaskResultImage("任务A").ptr<uchar>(0)[0] == 23 &&
        ToolController::GetTaskResultImage("任务B").ptr<uchar>(0)[0] == 187,
        "task result images were not retained independently");

    ToolController::RequestRunTaskGroup("任务A", false, false);
    for (int tick = 0;
        tick < 12 && ToolController::GetMode() != ToolController::Mode::Idle; ++tick)
    {
        ToolController::Tick();
    }
    Require(ToolChainState::ReadOnlyTools()[0].hasLastResult &&
        ToolChainState::ReadOnlyTools()[1].hasLastResult &&
        !ToolController::GetTaskResultImage("任务B").empty(),
        "rerunning one task cleared another task's results or image");

    std::error_code removeError;
    fs::remove(taskAPath, removeError);
    removeError.clear();
    fs::remove(taskBPath, removeError);
    ToolController::Reset();
    ToolChainState::ClearTools();
    ToolChainState::ReplaceTaskGroups({});
}

void TestToolControllerAdvancesTaskFolderImages()
{
    ToolController::Reset();
    ToolChainState::ClearTools();
    ToolChainState::ReplaceTaskGroups({});
    ImageState::SetImage(cv::Mat(8, 8, CV_8UC1, cv::Scalar(9)));

    namespace fs = std::filesystem;
    const fs::path folder = fs::temp_directory_path() /
        ("imgui_opencv_task_folder_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code fileError;
    fs::create_directories(folder, fileError);
    Require(!fileError, "task folder image test could not create folder");

    const fs::path firstPath = folder / "01.png";
    const fs::path secondPath = folder / "02.png";
    Require(cv::imwrite(firstPath.string(),
                cv::Mat(8, 8, CV_8UC1, cv::Scalar(31))) &&
            cv::imwrite(secondPath.string(),
                cv::Mat(8, 8, CV_8UC1, cv::Scalar(141))),
        "task folder image test could not create images");

    Require(ToolChainState::CreateTaskGroup("任务A") >= 0 &&
        ToolChainState::SetTaskGroupImageFolder(
            0, folder.string(), firstPath.string(), 2),
        "task folder image test setup failed");

    int firstCaptured = -1;
    int secondCaptured = -1;
    ToolInstance firstTool;
    firstTool.type = 2;
    firstTool.groupName = "任务A";
    firstTool.toolImpl = std::make_unique<TestInputCaptureTool>(&firstCaptured);
    ToolChainState::Tools().push_back(std::move(firstTool));
    ToolInstance secondTool;
    secondTool.type = 2;
    secondTool.groupName = "任务A";
    secondTool.toolImpl = std::make_unique<TestInputCaptureTool>(&secondCaptured);
    ToolChainState::Tools().push_back(std::move(secondTool));

    auto runTask = []()
    {
        ToolController::RequestRunTaskGroup("任务A", false, false);
        for (int tick = 0;
            tick < 12 && ToolController::GetMode() != ToolController::Mode::Idle;
            ++tick)
        {
            ToolController::Tick();
        }
    };

    runTask();
    Require(firstCaptured == 31 && secondCaptured == 31 &&
        ToolChainState::ReadOnlyTaskGroups()[0].imageFolderIndex == 0,
        "task folder did not start with the first image");
    runTask();
    Require(firstCaptured == 141 && secondCaptured == 141 &&
        ToolChainState::ReadOnlyTaskGroups()[0].imageFolderIndex == 1,
        "task folder did not advance independently to the second image");
    runTask();
    Require(firstCaptured == 31 && secondCaptured == 31 &&
        ToolChainState::ReadOnlyTaskGroups()[0].imageFolderIndex == 0,
        "task folder did not wrap back to the first image");

    firstCaptured = -1;
    secondCaptured = -1;
    ToolController::RequestStepReset();
    ToolController::RequestStepNextTaskGroup("任务A");
    ToolController::Tick();
    ToolController::RequestStepNextTaskGroup("任务A");
    ToolController::Tick();
    Require(firstCaptured == 141 && secondCaptured == 141 &&
        ToolChainState::ReadOnlyTaskGroups()[0].imageFolderIndex == 1,
        "task stepping changed images between tools in the same round");

    Require(ToolChainState::SetTaskGroupCameraPreferred(0, true),
        "task camera preference could not be enabled");
    ImageState::SetImage(cv::Mat(8, 8, CV_8UC1, cv::Scalar(203)));
    firstCaptured = -1;
    secondCaptured = -1;
    ToolController::ResumeRunAfterCamera(false, true);
    for (int tick = 0;
        tick < 12 && ToolController::GetMode() != ToolController::Mode::Idle;
        ++tick)
    {
        ToolController::Tick();
    }
    Require(firstCaptured == 203 && secondCaptured == 203 &&
        ToolChainState::ReadOnlyTaskGroups()[0].imageFolderIndex == 1,
        "camera-preferred task did not use the captured camera frame first");

    firstCaptured = -1;
    secondCaptured = -1;
    ImageState::SetImage(cv::Mat(8, 8, CV_8UC1, cv::Scalar(9)));
    ToolController::ResumeRunAfterCamera(false, false);
    for (int tick = 0;
        tick < 12 && ToolController::GetMode() != ToolController::Mode::Idle;
        ++tick)
    {
        ToolController::Tick();
    }
    Require(firstCaptured == 31 && secondCaptured == 31 &&
        ToolChainState::ReadOnlyTaskGroups()[0].imageFolderIndex == 0,
        "camera-preferred task did not fall back to its image folder");

    ToolController::Reset();
    ToolChainState::ClearTools();
    ToolChainState::ReplaceTaskGroups({});
    fs::remove_all(folder, fileError);
}

void TestUnboundToolDoesNotInheritCanvasROI()
{
    const cv::Mat input = cv::Mat::zeros(48, 64, CV_8UC1);
    ImageState::SetImage(input);
    ROIState::Clear();

    ROI canvasROI;
    canvasROI.type = ROI_TYPE_RECT;
    canvasROI.start = {8.0f, 10.0f};
    canvasROI.end = {28.0f, 30.0f};
    ROIState::Add(canvasROI, true);

    ToolInstance unbound;
    unbound.type = 3;
    ToolInstance snapshot;
    VisionContext context;
    Require(ToolExecutor::PrepareDetached(
        unbound, ImageState::Current(), 0, snapshot, context),
        "unbound tool snapshot preparation failed");
    Require(context.rois.empty(),
        "unbound tool inherited a canvas ROI instead of using the full image");

    std::vector<ToolInstance> taskTools{unbound};
    std::vector<int> taskIndices{0};
    Require(ToolExecutor::PrepareDetachedSnapshot(
        unbound, input, input, 0, taskTools, taskIndices,
        ROIState::ReadOnlyItems(), ROIState::SelectedIndex(), cv::Mat(),
        snapshot, context),
        "unbound task snapshot preparation failed");
    Require(context.rois.empty(),
        "unbound task snapshot inherited a visible canvas ROI");

    ToolInstance bound;
    bound.type = 3;
    bound.searchROIs.push_back(canvasROI);
    Require(ToolExecutor::PrepareDetached(
        bound, ImageState::Current(), 0, snapshot, context),
        "bound tool snapshot preparation failed");
    Require(context.rois.size() == 1 &&
        context.rois.front().ToCvRect() == canvasROI.ToCvRect(),
        "bound tool did not keep its explicit search ROI");

    ROIState::Clear();
}

void TestToolControllerRunsTaskGroupsInParallel()
{
    ToolController::Reset();
    ToolController::SetTaskParallelEnabled(true);
    ToolChainState::ClearTools();
    ToolChainState::ReplaceTaskGroups({});
    ImageState::SetImage(cv::Mat(32, 32, CV_8UC1, cv::Scalar(9)));

    const fs::path suffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const fs::path taskAPath = fs::temp_directory_path() /
        ("imgui_opencv_parallel_a_" + suffix.string() + ".png");
    const fs::path taskBPath = fs::temp_directory_path() /
        ("imgui_opencv_parallel_b_" + suffix.string() + ".png");
    Require(cv::imwrite(taskAPath.string(),
            cv::Mat(32, 32, CV_8UC1, cv::Scalar(23))) &&
        cv::imwrite(taskBPath.string(),
            cv::Mat(32, 32, CV_8UC1, cv::Scalar(187))),
        "task-parallel test could not create input images");
    Require(ToolChainState::CreateTaskGroup("任务A") >= 0 &&
        ToolChainState::CreateTaskGroup("任务B") >= 0 &&
        ToolChainState::SetTaskGroupImagePath(0, taskAPath.string()) &&
        ToolChainState::SetTaskGroupImagePath(1, taskBPath.string()),
        "task-parallel group setup failed");

    auto addThreshold = [](const char* groupName)
    {
        ToolInstance tool;
        tool.type = 3;
        tool.groupName = groupName;
        tool.inputSourceMode = 0;
        tool.threshold.useGray = true;
        tool.threshold.enableThreshold = true;
        tool.threshold.threshold = 100;
        ToolChainState::AddTool(std::move(tool));
    };
    addThreshold("任务A");
    addThreshold("任务B");
    addThreshold("任务A");
    addThreshold("任务B");

    ToolController::RequestRunAll(false, false);
    Require(ToolController::IsParallelExecutionActive(),
        "run-all did not launch independent task groups in parallel");
    for (int attempt = 0; attempt < 500 &&
        ToolController::GetMode() != ToolController::Mode::Idle; ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        ToolController::Tick();
    }

    const cv::Mat taskAResult = ToolController::GetTaskResultImage("任务A");
    const cv::Mat taskBResult = ToolController::GetTaskResultImage("任务B");
    Require(ToolController::GetMode() == ToolController::Mode::Idle &&
        ToolChainState::AtReadOnly(0)->hasLastResult &&
        ToolChainState::AtReadOnly(1)->hasLastResult &&
        ToolChainState::AtReadOnly(2)->hasLastResult &&
        ToolChainState::AtReadOnly(3)->hasLastResult,
        "task-parallel run did not publish every task result");
    Require(!taskAResult.empty() && !taskBResult.empty() &&
        cv::countNonZero(taskAResult.reshape(1)) == 0 &&
        cv::countNonZero(taskBResult.reshape(1)) ==
            static_cast<int>(taskBResult.total() * taskBResult.channels()),
        "task-parallel result images were mixed between tasks");

    std::error_code removeError;
    fs::remove(taskAPath, removeError);
    removeError.clear();
    fs::remove(taskBPath, removeError);
    ToolController::SetTaskParallelEnabled(false);
    ToolController::Reset();
    ToolChainState::ClearTools();
    ToolChainState::ReplaceTaskGroups({});
}

void TestToolControllerPublishesHeavyToolAsync()
{
    const std::vector<ROI> previousROIs = ROIState::ReadOnlyItems();
    const int previousSelectedROI = ROIState::SelectedIndex();
    ToolController::Reset();
    ToolChainState::ClearTools();
    ROIState::Clear();

    cv::Mat input(80, 100, CV_8UC1, cv::Scalar(0));
    ImageState::SetImage(input);

    ROI line;
    line.type = ROI_TYPE_LINE;
    line.start = ImVec2(10.0f, 10.0f);
    line.end = ImVec2(40.0f, 50.0f);
    ROIState::Add(line, true);

    ToolInstance measurement;
    measurement.type = 15;
    measurement.measureMode = 0;
    measurement.measureMmPerPixel = 0.1f;
    measurement.useSearchROI = true;
    measurement.searchROIs.push_back(line);
    const int index = ToolChainState::AddTool(std::move(measurement));

    ToolController::RequestRun(index);
    ToolController::Tick();
    Require(!ToolChainState::AtReadOnly(index)->hasLastResult,
        "heavy tool result was published in the worker launch frame");

    for (int attempt = 0; attempt < 200 && !ToolChainState::AtReadOnly(index)->hasLastResult; ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        ToolController::Tick();
    }
    const ToolInstance* completed = ToolChainState::AtReadOnly(index);
    Require(completed && completed->hasLastResult && completed->lastResult.success,
        "heavy tool background result was not published by ToolController");
    Require(!completed->lastResult.measurements.empty(),
        "heavy tool background result lost measurement values");

    ToolInstance* mutableMeasurement = ToolChainState::At(index);
    Require(mutableMeasurement != nullptr, "heavy tool disappeared before revision test");
    mutableMeasurement->hasLastResult = false;
    ToolController::RequestRun(index);
    ToolController::Tick();
    mutableMeasurement->MarkParametersChanged();
    for (int attempt = 0; attempt < 200; ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        ToolController::Tick();
    }
    Require(!mutableMeasurement->hasLastResult,
        "background result from an older parameter revision was published");

    ToolController::RequestRun(index);
    ToolController::Tick();
    for (int attempt = 0; attempt < 200 && !mutableMeasurement->hasLastResult; ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        ToolController::Tick();
    }
    Require(mutableMeasurement->hasLastResult,
        "latest parameter revision did not publish after a stale result was discarded");

    ToolController::Reset();
    ToolChainState::ClearTools();
    ROIState::ReplaceAll(previousROIs);
    ROIState::SetSelectedIndex(previousSelectedROI);
}

void TestToolControllerExecutesDagLevelInParallel()
{
    const std::vector<ROI> previousROIs = ROIState::ReadOnlyItems();
    const int previousSelectedROI = ROIState::SelectedIndex();
    ToolController::OnToolChainChanged();
    ToolChainState::ClearTools();
    ROIState::Clear();
    ImageState::SetImage(cv::Mat(80, 100, CV_8UC1, cv::Scalar(0)));

    ROI line;
    line.type = ROI_TYPE_LINE;
    line.start = ImVec2(10.0f, 10.0f);
    line.end = ImVec2(50.0f, 40.0f);
    ROIState::Add(line, true);

    ToolInstance first;
    first.type = 15;
    first.inputSourceMode = 2;
    first.measureMode = 0;
    first.measureMmPerPixel = 0.1f;
    first.useSearchROI = true;
    first.searchROIs.push_back(line);
    const int firstIndex = ToolChainState::AddTool(std::move(first));

    ToolInstance second;
    second.type = 15;
    second.inputSourceMode = 2;
    second.measureMode = 0;
    second.measureMmPerPixel = 0.2f;
    second.useSearchROI = true;
    second.searchROIs.push_back(line);
    const int secondIndex = ToolChainState::AddTool(std::move(second));

    ToolController::RequestRunAll(false, false);
    ToolController::Tick();
    Require(ToolController::IsParallelExecutionActive() &&
        !ToolChainState::AtReadOnly(firstIndex)->hasLastResult &&
        !ToolChainState::AtReadOnly(secondIndex)->hasLastResult,
        "ToolController did not launch an independent DAG level in parallel");

    for (int attempt = 0; attempt < 500 &&
        ToolController::GetMode() != ToolController::Mode::Idle; ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        ToolController::Tick();
    }
    Require(ToolController::GetMode() == ToolController::Mode::Idle &&
        ToolChainState::AtReadOnly(firstIndex)->hasLastResult &&
        ToolChainState::AtReadOnly(secondIndex)->hasLastResult,
        "parallel DAG level did not publish every tool result");

    ToolController::RequestRunAll(false, false);
    ToolController::Tick();
    Require(ToolController::IsParallelExecutionActive(),
        "repeated DAG batch did not start a new execution round");
    for (int attempt = 0; attempt < 500 &&
        ToolController::GetMode() != ToolController::Mode::Idle; ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        ToolController::Tick();
    }
    Require(ToolController::GetMode() == ToolController::Mode::Idle &&
        ToolController::GetToolTimeMs(firstIndex) > 0.0f &&
        ToolController::GetToolTimeMs(secondIndex) > 0.0f &&
        ToolController::GetTotalTimeMs() > 0.0f,
        "repeated DAG batch incorrectly reused results from the previous run");

    ToolController::OnToolChainChanged();
    ToolChainState::ClearTools();
    ROIState::ReplaceAll(previousROIs);
    ROIState::SetSelectedIndex(previousSelectedROI);
}

void TestCancellationTokensStopHeavyTools()
{
    VisionContext context;
    context.image = cv::Mat::zeros(48, 48, CV_8UC1);
    std::stop_source cancellation;
    context.stopToken = cancellation.get_token();
    cancellation.request_stop();

    TemplateMatchingTool templateTool;
    templateTool.templateImg = cv::Mat::zeros(8, 8, CV_8UC1);
    const ToolResult templateResult = templateTool.Execute(context);
    Require(!templateResult.success && templateResult.message == "执行已取消",
        "template matching ignored a requested cancellation");

    WindowsPPOCREngine engine;
    std::vector<PPOCRTextResult> texts;
    std::string error;
    Require(!engine.Recognize(cv::Mat::zeros(16, 16, CV_8UC3), texts, &error,
            cancellation.get_token()) && error == "OCR execution cancelled",
        "OCR engine ignored a requested cancellation");
}

void TestAsyncHeavyToolUsesDependencySnapshots()
{
    const std::vector<ROI> previousROIs = ROIState::ReadOnlyItems();
    const int previousSelectedROI = ROIState::SelectedIndex();
    ToolController::Reset();
    ToolChainState::ClearTools();
    ROIState::Clear();

    cv::Mat input = cv::Mat::zeros(80, 100, CV_8UC1);
    cv::rectangle(input, cv::Rect(20, 20, 24, 18), cv::Scalar(220), cv::FILLED);
    ImageState::SetImage(input);

    ToolInstance upstream;
    upstream.type = 2;
    upstream.hasLastResult = true;
    ToolResult::Region located;
    located.bbox = cv::Rect(16, 16, 36, 30);
    located.angle = 0.0f;
    upstream.lastResult.regions.push_back(located);
    const int upstreamIndex = ToolChainState::AddTool(std::move(upstream));
    const ToolInstance* upstreamTool = ToolChainState::AtReadOnly(upstreamIndex);
    Require(upstreamTool != nullptr, "dependency snapshot upstream setup failed");

    ToolInstance consumer;
    consumer.type = 1;
    consumer.templateImg = input(cv::Rect(20, 20, 24, 18)).clone();
    consumer.matchThreshold = 0.5f;
    consumer.resultRoiMode = static_cast<int>(ResultROIMode::NthResult);
    consumer.resultRoiSourceToolId = upstreamTool->toolId;
    consumer.fixture.enabled = true;
    consumer.fixture.sourceToolId = upstreamTool->toolId;
    consumer.fixture.referenceOrigin = cv::Point2f(34.0f, 31.0f);
    const int consumerIndex = ToolChainState::AddTool(std::move(consumer));

    ToolController::RequestRun(consumerIndex);
    ToolController::Tick();
    Require(!ToolChainState::AtReadOnly(consumerIndex)->hasLastResult,
        "Fixture/result-ROI heavy tool fell back to synchronous execution");

    for (int attempt = 0;
         attempt < 200 && !ToolChainState::AtReadOnly(consumerIndex)->hasLastResult;
         ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        ToolController::Tick();
    }
    Require(ToolChainState::AtReadOnly(consumerIndex)->hasLastResult,
        "dependency snapshot heavy tool did not publish asynchronously");

    ToolController::Reset();
    ToolChainState::ClearTools();
    ROIState::ReplaceAll(previousROIs);
    ROIState::SetSelectedIndex(previousSelectedROI);
}

void TestToolControllerLoopTimingResetsEachRound()
{
    const int previousLoopIntervalMs = ToolController::GetLoopIntervalMs();
    const std::uint64_t initialCompletedSerial = ToolController::GetCompletedBatchSerial();
    ToolController::Reset();
    ToolChainState::ClearTools();
    ImageState::SetImage(cv::Mat(16, 16, CV_8UC1, cv::Scalar(25)));

    int captured = -1;
    ToolInstance tool;
    tool.type = 2;
    tool.toolImpl = std::make_unique<TestInputCaptureTool>(&captured, -1, 12);
    ToolChainState::AddTool(std::move(tool));
    ToolController::SetLoopIntervalMs(40);

    ToolController::RequestRunAll(true, false);
    ToolController::Tick();
    const float firstRoundMs = ToolController::GetTotalTimeMs();
    Require(firstRoundMs >= 8.0f,
        "loop timing test did not execute the first delayed round");
    Require(ToolController::GetLoopIteration() == 1 &&
        ToolController::IsWaitingForNextLoop() &&
        ToolController::GetLoopWaitRemainingMs() > 0,
        "loop round or wait countdown state was not published after the first round");
    Require(ToolController::GetCompletedBatchSerial() == initialCompletedSerial + 1 &&
        ToolController::GetLastCompletedLoopRound() == 1,
        "first loop round did not publish a completed batch event");

    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    ToolController::Tick();
    const float secondRoundMs = ToolController::GetTotalTimeMs();
    Require(secondRoundMs >= 8.0f,
        "loop timing test did not execute the second delayed round");
    // Windows scheduling can make a short first round land close to the
    // lower bound. Allow that jitter while still rejecting a value that
    // clearly contains the previous round's elapsed time.
    // A short sleep plus Windows timer granularity can add several
    // milliseconds to the second round. Keep enough margin for that
    // scheduling jitter while still rejecting a clearly accumulated round.
    Require(secondRoundMs < (std::max)(firstRoundMs * 3.0f, firstRoundMs + 30.0f),
        "loop total time accumulated previous rounds");
    Require(ToolController::GetLoopIteration() == 2,
        "loop iteration did not advance after the second round");
    Require(ToolController::GetCompletedBatchSerial() == initialCompletedSerial + 2 &&
        ToolController::GetLastCompletedLoopRound() == 2,
        "second loop round did not refresh the completed batch event");

    ToolController::SetLoopEnabled(false);
    Require(ToolController::GetLastCompletedLoopRound() == 2,
        "stopping the loop discarded the latest completed round metadata");
    ToolController::SetLoopIntervalMs(previousLoopIntervalMs);
    ToolChainState::ClearTools();
}

void TestToolControllerLoopSurvivesDebugImagePublication()
{
    const int previousLoopIntervalMs = ToolController::GetLoopIntervalMs();
    const std::uint64_t initialCompletedSerial =
        ToolController::GetCompletedBatchSerial();
    ToolController::Reset();
    ToolChainState::ClearTools();
    ToolChainState::ReplaceTaskGroups({});
    ImageState::SetImage(cv::Mat(80, 100, CV_8UC1, cv::Scalar(25)));

    ROI line;
    line.type = ROI_TYPE_LINE;
    line.start = ImVec2(10.0f, 10.0f);
    line.end = ImVec2(40.0f, 50.0f);

    ToolInstance measurement;
    measurement.type = 15;
    measurement.inputSourceMode = 2;
    measurement.measureMode = 0;
    measurement.useSearchROI = true;
    measurement.searchROIs.push_back(line);
    ToolChainState::AddTool(std::move(measurement));
    ToolController::SetLoopIntervalMs(1000);

    ToolController::RequestRunAll(true, false);
    ToolController::Tick();
    Require(ToolController::GetMode() == ToolController::Mode::Running,
        "async loop did not start");

    const int sourceVersion = ImageState::Version();
    ImageState::SetDebugImage(cv::Mat(80, 100, CV_8UC1, cv::Scalar(70)));
    Require(ImageState::Version() == sourceVersion,
        "debug image publication invalidated an in-flight loop input");

    for (int attempt = 0; attempt < 300 &&
        ToolController::GetCompletedBatchSerial() == initialCompletedSerial; ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        ToolController::Tick();
    }

    Require(ToolController::GetCompletedBatchSerial() == initialCompletedSerial + 1 &&
        ToolController::IsLoopEnabled() &&
        ToolController::GetMode() == ToolController::Mode::Running &&
        ToolController::IsWaitingForNextLoop(),
        "async loop stopped after a debug image was published");

    ToolController::SetLoopEnabled(false);
    ToolController::SetLoopIntervalMs(previousLoopIntervalMs);
    ToolChainState::ClearTools();
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
    const SpcDatabaseSnapshot databaseSnapshot = SpcDatabase::Snapshot();
    Require(databaseSnapshot.open && databaseSnapshot.available &&
        SpcDatabase::LoadRecent(10).size() == 4,
        "SPC SQLite persistence did not store measurement samples");

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

void TestToolExecutionGraphTopologyAndCache()
{
    std::vector<ToolInstance> tools;
    ToolInstance original;
    original.type = 12;
    original.toolId = 10;
    tools.push_back(std::move(original));

    ToolInstance yolo;
    yolo.type = 4;
    yolo.toolId = 20;
    yolo.inputSourceMode = 2;
    tools.push_back(std::move(yolo));

    ToolInstance ocr;
    ocr.type = 13;
    ocr.toolId = 30;
    ocr.inputSourceMode = 2;
    tools.push_back(std::move(ocr));

    ToolInstance measurement;
    measurement.type = 15;
    measurement.toolId = 40;
    measurement.inputSourceMode = 2;
    measurement.resultRoiMode = static_cast<int>(ResultROIMode::SelectedPair);
    measurement.resultRoiSourceToolId = 20;
    measurement.resultRoiSecondSourceToolId = 30;
    tools.push_back(std::move(measurement));

    const ToolExecutionGraphPlan plan = ToolExecutionGraph::Build(tools);
    Require(plan.valid && plan.levels.size() == 3 &&
        plan.levels[0] == std::vector<int>{0} &&
        plan.levels[1] == std::vector<int>({1, 2}) &&
        plan.levels[2] == std::vector<int>{3} &&
        plan.nodes[3].dependencies == std::vector<int>({0, 1, 2}) &&
        plan.nodes[1].parallelizable && plan.nodes[2].parallelizable,
        "tool execution DAG did not create the expected parallel branches");

    ToolExecutionCacheKey key;
    key.toolId = 20;
    key.parameterRevision = 3;
    key.runRevision = 11;
    key.imageVersion = 7;
    key.upstreamRevision = ToolExecutionGraph::ComputeUpstreamRevision(plan, tools, 1);
    ToolResult expected;
    expected.toolName = "cached-yolo";
    expected.status = ToolResultStatus::Pass;
    ToolExecutionGraph::StoreCachedResult(key, expected);
    ToolResult actual;
    Require(ToolExecutionGraph::TryGetCachedResult(key, actual) &&
        actual.toolName == expected.toolName,
        "tool execution cache lookup regressed");
    ToolExecutionCacheKey nextRunKey = key;
    ++nextRunKey.runRevision;
    Require(!ToolExecutionGraph::TryGetCachedResult(nextRunKey, actual),
        "tool execution cache leaked across execution rounds");
    ToolExecutionGraph::ClearCache();

    tools[1].fixture.enabled = true;
    tools[1].fixture.sourceToolId = 40;
    const ToolExecutionGraphPlan cycle = ToolExecutionGraph::Build(tools);
    Require(!cycle.valid && !cycle.error.empty(),
        "tool execution DAG failed to reject a dependency cycle");
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
    ROIState::Clear();

    ROI configured;
    configured.runtimeId = 42;
    configured.type = ROI_TYPE_LINE;
    configured.start = {5.0f, 10.0f};
    configured.end = {25.0f, 10.0f};

    ROI moved = configured;
    moved.start = {60.0f, 50.0f};
    moved.end = {90.0f, 50.0f};
    ROIState::Add(moved, false);

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

void TestShapeModelRotationSearch()
{
    cv::Mat tpl = cv::Mat::zeros(36, 44, CV_8UC1);
    cv::rectangle(tpl, cv::Rect(5, 5, 8, 26), cv::Scalar(255), cv::FILLED);
    cv::rectangle(tpl, cv::Rect(5, 23, 30, 8), cv::Scalar(255), cv::FILLED);
    const cv::Point2f center(tpl.cols * 0.5f, tpl.rows * 0.5f);
    cv::Mat transform = cv::getRotationMatrix2D(center, 30.0, 1.0);
    const cv::Rect bounds = cv::RotatedRect(center, tpl.size(), 30.0).boundingRect();
    transform.at<double>(0, 2) += bounds.width * 0.5 - center.x;
    transform.at<double>(1, 2) += bounds.height * 0.5 - center.y;
    cv::Mat rotated;
    cv::warpAffine(tpl, rotated, transform, bounds.size(), cv::INTER_LINEAR,
        cv::BORDER_CONSTANT, cv::Scalar(0));
    cv::Mat image = cv::Mat::zeros(140, 180, CV_8UC1);
    rotated.copyTo(image(cv::Rect(70, 45, rotated.cols, rotated.rows)));

    ShapeMatcher::Params params;
    params.blurSize = 0;
    params.tplMinArea = 10.0;
    params.minScore = 0.75;
    params.minShapeScore = 0.5;
    params.maxResults = 1;
    params.enableRotation = true;
    params.rotationStart = 20;
    params.rotationEnd = 40;
    params.rotationStep = 5;
    const auto matches = ShapeMatcher::Search(image, tpl, params, {});
    Require(!matches.empty() && std::abs(matches.front().angle - 30.0f) <= 5.0f &&
        std::abs(matches.front().bbox.x - 70) <= 2 &&
        std::abs(matches.front().bbox.y - 45) <= 2,
        "HALCON-style shape model rotation search regressed");
}

void TestRenderBackendSelectionPolicy()
{
    Require(SelectRenderBackend(true, true) == RenderBackendKind::DirectX12,
        "renderer policy did not prefer DX12 when both backends are available");
    Require(SelectRenderBackend(false, true) == RenderBackendKind::DirectX11,
        "renderer policy did not fall back to DX11 after DX12 failure");
    Require(SelectRenderBackend(false, false) == RenderBackendKind::None,
        "renderer policy selected a backend when none were available");
}

void TestRunResultLayoutPolicy()
{
    namespace Layout = UI::RunResultLayout;

    const Layout::Size fullHd =
        Layout::CalculateDashboardWindowSize(1920.0f, 1080.0f);
    Require(std::abs(fullHd.width - 1760.0f) < 0.01f &&
        std::abs(fullHd.height - 993.6f) < 0.01f,
        "run-result dashboard window sizing regressed");

    const Layout::DashboardGrid threeCards =
        Layout::CalculateDashboardGrid(3, 1200.0f, 700.0f, 8.0f);
    Require(threeCards.columns == 3 && threeCards.rowCount == 1 &&
        threeCards.visibleRowCount == 1 &&
        std::abs(threeCards.cardHeight - 520.0f) < 0.01f,
        "run-result single-row grid layout regressed");

    const Layout::DashboardGrid wrappedCards =
        Layout::CalculateDashboardGrid(8, 900.0f, 700.0f, 8.0f);
    Require(wrappedCards.columns == 3 && wrappedCards.rowCount == 3 &&
        wrappedCards.visibleRowCount == 2 &&
        std::abs(wrappedCards.cardHeight - 342.0f) < 0.01f,
        "run-result wrapped grid layout regressed");
    Require(Layout::CalculateDashboardGrid(0, 900.0f, 700.0f, 8.0f).columns == 0,
        "empty run-result grid should not create columns");
    Require(Layout::FormatDuration(0.0f) == "<0.1 ms" &&
        Layout::FormatDuration(11.66f) == "11.7 ms" &&
        Layout::FormatDuration(1239.2f) == "1.239 s" &&
        Layout::FormatDuration(-1.0f) == "--" &&
        Layout::FormatDuration(0.0f, 2) == "<0.01 ms",
        "run-result duration formatting regressed");

    const Layout::Rect imageBounds{0.0f, 0.0f, 200.0f, 100.0f};
    const Layout::LabelPlacement first = Layout::PlaceOverlayLabel(
        {20.0f, 20.0f}, {50.0f, 10.0f}, imageBounds, {});
    Require(first.placed && first.bounds.left >= imageBounds.left &&
        first.bounds.top >= imageBounds.top &&
        first.bounds.right <= imageBounds.right &&
        first.bounds.bottom <= imageBounds.bottom,
        "run-result overlay label was not clamped to the image");

    const Layout::LabelPlacement second = Layout::PlaceOverlayLabel(
        {20.0f, 20.0f}, {50.0f, 10.0f}, imageBounds, {first.bounds});
    Require(second.placed && !Layout::RectsOverlap(first.bounds, second.bounds),
        "run-result overlay labels were not separated");
    Require(!Layout::RectsOverlap(
        {0.0f, 0.0f, 10.0f, 10.0f}, {10.0f, 0.0f, 20.0f, 10.0f}),
        "touching run-result label edges should not overlap");

    const Layout::LabelPlacement saturated = Layout::PlaceOverlayLabel(
        {20.0f, 20.0f}, {50.0f, 10.0f}, imageBounds, {imageBounds});
    Require(!saturated.placed,
        "run-result overlay label should be skipped when no slot is free");
}

void TestIndustrialCameraPixelFormatMatrix()
{
    const CameraPixelFormatDescription mono10 =
        DescribeCameraPixelFormat(0x01100003U);
    const CameraPixelFormatDescription mono12Packed =
        DescribeCameraPixelFormat(0x010C0006U);
    const CameraPixelFormatDescription mono16 =
        DescribeCameraPixelFormat(0x01100007U);
    const CameraPixelFormatDescription bayer12 =
        DescribeCameraPixelFormat(0x01100011U);
    Require(mono10.name == "Mono10" && mono10.bitDepth == 10 &&
        mono10.storageBitsPerPixel == 16 && mono10.monochrome,
        "Mono10 PFNC description regressed");
    Require(mono12Packed.name == "Mono12Packed" && mono12Packed.bitDepth == 12 &&
        mono12Packed.storageBitsPerPixel == 12,
        "Mono12Packed PFNC description regressed");
    Require(mono16.name == "Mono16" && mono16.bitDepth == 16,
        "Mono16 PFNC description regressed");
    Require(bayer12.name == "BayerRG12" && bayer12.bayer &&
        bayer12.bitDepth == 12,
        "Bayer12 PFNC description regressed");
}

int RunCameraDiscoveryDiagnostic(const std::string& backend)
{
    std::unique_ptr<ICameraAdapter> camera;
    if (backend == "huaray")
        camera = std::make_unique<HuarayImvCameraAdapter>();
    else if (backend == "hikrobot")
        camera = std::make_unique<HikrobotMvsCameraAdapter>();
    else
    {
        std::cerr << "unknown camera backend: " << backend << "\n";
        return 2;
    }
    std::vector<CameraDeviceInfo> devices;
    const DeviceOperationResult result = camera->EnumerateDevices(devices);
    std::cout << camera->AdapterName() << ": " << result.message << "\n";
    for (const CameraDeviceInfo& device : devices)
    {
        std::cout << "selector=" << device.selector << " model=" << device.model
                  << " serial=" << device.serialNumber << " ip=" << device.ipAddress
                  << " mac=" << device.macAddress << " transport=" << device.transport
                  << " status=" << device.status << " sdk=" << device.runtimeVersion
                  << " path=" << device.runtimePath << "\n";
    }
    std::cout << "device_count=" << devices.size() << "\n";
    return result.success ? 0 : 1;
}

int RunCameraPixelFormatDiagnostic(const std::string& backend,
    const std::string& selector, int requestedFrames, const std::string& reportPath)
{
    std::unique_ptr<ICameraAdapter> camera;
    if (backend == "huaray")
        camera = std::make_unique<HuarayImvCameraAdapter>();
    else if (backend == "hikrobot")
        camera = std::make_unique<HikrobotMvsCameraAdapter>();
    else
        return 2;

    DeviceEndpoint endpoint;
    endpoint.address = selector;
    endpoint.timeoutMs = 3000;
    DeviceOperationResult operation = camera->Connect(endpoint);
    if (!operation.success)
    {
        std::cerr << "connect failed: " << operation.message << "\n";
        return 1;
    }
    operation = camera->StartStream();
    if (!operation.success)
    {
        std::cerr << "start stream failed: " << operation.message << "\n";
        camera->Disconnect();
        return 1;
    }

    nlohmann::json frames = nlohmann::json::array();
    int successfulFrames = 0;
    for (int index = 0; index < (std::max)(1, requestedFrames); ++index)
    {
        cv::Mat frame;
        CameraFrameMetadata metadata;
        operation = camera->GrabFrame(frame, metadata, 3000);
        if (!operation.success)
        {
            std::cerr << "frame " << (index + 1) << " failed: " << operation.message << "\n";
            continue;
        }
        const bool validDisplayFrame = !frame.empty() &&
            (frame.type() == CV_8UC1 || frame.type() == CV_8UC3);
        frames.push_back({
            {"index", index + 1}, {"frameNumber", metadata.frameNumber},
            {"sourcePfnc", metadata.sourcePixelFormat},
            {"sourcePixelFormat", metadata.sourcePixelFormatName},
            {"sourceBitDepth", metadata.sourceBitDepth},
            {"sourceStorageBitsPerPixel", metadata.sourceStorageBitsPerPixel},
            {"sourceIsBayer", metadata.sourceIsBayer},
            {"convertedToDisplay", metadata.convertedToDisplay},
            {"conversionPath", metadata.conversionPath},
            {"width", frame.cols}, {"height", frame.rows},
            {"displayFrameValid", validDisplayFrame}
        });
        successfulFrames += validDisplayFrame ? 1 : 0;
        std::cout << "frame=" << (index + 1) << " source="
                  << metadata.sourcePixelFormatName << " bitDepth="
                  << metadata.sourceBitDepth << " bayer=" << metadata.sourceIsBayer
                  << " conversion=" << metadata.conversionPath << " display="
                  << frame.cols << "x" << frame.rows << " type=" << frame.type() << "\n";
    }
    camera->StopStream();
    camera->Disconnect();

    const nlohmann::json report = {
        {"kind", "camera_pixel_format_validation"}, {"backend", backend},
        {"selector", selector}, {"requestedFrames", requestedFrames},
        {"successfulFrames", successfulFrames}, {"frames", frames}
    };
    if (!reportPath.empty())
    {
        std::ofstream output(reportPath, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            std::cerr << "cannot write report: " << reportPath << "\n";
            return 1;
        }
        output << report.dump(2);
    }
    return successfulFrames > 0 ? 0 : 1;
}
}

int main(int argc, char** argv)
{
    try {
        const fs::path testSpcDatabase = fs::temp_directory_path() /
            ("imgui_opencv_spc_test_" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");
        InspectionHistory::ConfigureDatabase(testSpcDatabase.string());
        if (argc > 1 && std::string(argv[1]) == "--huaray-discovery-only")
            return RunCameraDiscoveryDiagnostic("huaray");
        if (argc > 1 && std::string(argv[1]) == "--hikrobot-discovery-only")
            return RunCameraDiscoveryDiagnostic("hikrobot");
        if (argc > 3 && std::string(argv[1]) == "--camera-pixel-format")
        {
            const int frames = argc > 4 ? (std::max)(1, std::atoi(argv[4])) : 10;
            const std::string reportPath = argc > 5 ? argv[5]
                : "camera_pixel_format_validation.json";
            return RunCameraPixelFormatDiagnostic(argv[2], argv[3], frames, reportPath);
        }
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
            TestAllToolRegistryAndResultCapabilityContracts();
            TestTemplateMatchingToolUsesInstanceParameters();
            TestToolChainReorderRemapsResultROISource();
            TestRecipeRoundTrip();
            TestTaskGroupManagement();
            TestToolInstanceOwnsRecipeSerialization();
            TestToolExecutorResolvesMovedRuntimeRoi();
            TestRenderBackendSelectionPolicy();
            TestRunResultLayoutPolicy();
            TestIndustrialCameraPixelFormatMatrix();
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
        if (argc > 1 && std::string(argv[1]) == "--original-result-only") {
            TestOriginalImageToolPublishesResult();
            std::cout << "regression_tests: original image result checks passed\n";
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--task-images-only") {
            TestRecipeRoundTrip();
            TestToolControllerUsesIndependentTaskImages();
            TestToolControllerAdvancesTaskFolderImages();
            TestToolControllerRunsTaskGroupsInParallel();
            std::cout << "regression_tests: independent task image checks passed\n";
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--roi-domain-only") {
            TestRotatedROIExtractionAndResultRestore();
            TestToolExecutorUsesRotatedROI();
            TestHalconStyleROIDomain();
            std::cout << "regression_tests: HALCON ROI domain checks passed\n";
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--hardware-camera-only") {
            TestHardwareRuntimeAutomation();
            std::cout << "regression_tests: hardware camera automation checks passed\n";
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--plc-handshake-only") {
            TestHardwareSettingsPersistence();
            TestHardwareTaskTriggerMappingSynchronization();
            TestModbusHandshakeTriggersIndependentTaskCameraRun();
            std::cout << "regression_tests: PLC handshake checks passed\n";
            return 0;
        }
        TestTemplateMatch();
        TestRoiConversion();
        TestRotatedROIExtractionAndResultRestore();
        TestToolExecutorUsesRotatedROI();
        TestHalconStyleROIDomain();
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
        TestAllToolRegistryAndResultCapabilityContracts();
        TestTemplateMatchingToolUsesInstanceParameters();
        TestToolChainReorderRemapsResultROISource();
        TestRecipeRoundTrip();
        TestTaskGroupManagement();
        TestToolInstanceOwnsRecipeSerialization();
        TestSampleImageCorePipeline();
        TestContourDirectionAndSubpixelBoundary();
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
        TestModbusHandshakeTriggersIndependentTaskCameraRun();
        TestFrameArchiveService();
        TestRecipeAutosaveService();
        TestHardwareSettingsPersistence();
        TestHardwareTaskTriggerMappingSynchronization();
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
        TestUnboundToolDoesNotInheritCanvasROI();
        TestToolExecutorDetachedExecutionPublishesOnCallerThread();
        TestToolExecutorResolvesMovedRuntimeRoi();
        TestToolChainEditActions();
        TestToolChainValidatorAndRunGuard();
        TestToolChainPreflight();
        TestToolExecutionGraphTopologyAndCache();
        TestToolChainDuplicate();
        TestOriginalImageToolPublishesResult();
        TestToolControllerInputSourcesAndChainReset();
        TestToolControllerRunsOnlySelectedTaskGroupInOrder();
        TestToolControllerStepsOnlySelectedTaskGroupInOrder();
        TestToolControllerUsesIndependentTaskImages();
        TestToolControllerAdvancesTaskFolderImages();
        TestToolControllerRunsTaskGroupsInParallel();
        TestToolControllerPublishesHeavyToolAsync();
        TestToolControllerExecutesDagLevelInParallel();
        TestCancellationTokensStopHeavyTools();
        TestAsyncHeavyToolUsesDependencySnapshots();
        TestToolControllerLoopTimingResetsEachRound();
        TestToolControllerLoopSurvivesDebugImagePublication();
        TestExampleRecipesLoadAndExecute();
        TestShapeMaxResultsDefaultIsOne();
        TestToolInstanceLabelDefaultIsEmpty();
        TestCoreStateOwnsRoiAndToolChain();
        TestShapeMatcherTemplateLargerThanSearchImage();
        TestShapeModelRotationSearch();
        TestResultOverlayStatePolicy();
        TestRenderBackendSelectionPolicy();
        TestRunResultLayoutPolicy();
        TestIndustrialCameraPixelFormatMatrix();
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
