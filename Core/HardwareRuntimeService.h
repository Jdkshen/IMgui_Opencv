#pragma once

#include "HardwareAdapters.h"
#include "../Algorithm/ToolResult.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum class HardwareOutputKind
{
    PlcTag,
    ModbusCoil,
    OpcUaNode,
    TcpText
};

struct HardwareOutputBinding
{
    HardwareOutputKind kind = HardwareOutputKind::PlcTag;
    std::string adapterKey;
    std::string target;
    std::uint16_t address = 0;
    bool invert = false;
    std::string passText = "PASS";
    std::string failText = "FAIL";
    bool appendCrLf = true;
};

enum class HardwareOutputAdapterType
{
    ModbusTcp,
    ModbusPlc,
    OpcUa,
    TcpText
};

enum class HardwareIoSignal
{
    Trigger,
    Busy,
    Done,
    Ok,
    Ng,
    Error,
    Heartbeat,
    Acknowledge
};

enum class HardwareIoDirection
{
    Input,
    Output
};

struct HardwareIoMapping
{
    bool enabled = true;
    HardwareIoSignal signal = HardwareIoSignal::Trigger;
    HardwareIoDirection direction = HardwareIoDirection::Input;
    std::uint16_t address = 0;
    bool invert = false;
    int pulseMs = 0;
    std::string taskGroupName;
};

struct HardwareHandshakeConfig
{
    bool enabled = false;
    std::vector<HardwareIoMapping> mappings;
    int pollIntervalMs = 50;
    int acknowledgementTimeoutMs = 3000;
    int inspectionTimeoutMs = 30000;
    int heartbeatIntervalMs = 1000;
    bool autoReconnect = true;
    int reconnectFailureThreshold = 3;
    int reconnectInitialDelayMs = 250;
    int reconnectMaxDelayMs = 5000;
};

struct HardwareCameraConnectionConfig
{
    int slotIndex = -1;
    DeviceEndpoint endpoint;
    std::string sourceName = "industrial-camera";
    int grabTimeoutMs = 250;
    int captureIntervalMs = 33;
    int orientation = 0;
    bool autoCapture = true;
    bool triggerOnInspection = true;
    bool autoExposure = true;
    double exposure = -6.0;
    double gain = 0.0;
    CameraTriggerConfig trigger;
    CameraBufferPolicy bufferPolicy = CameraBufferPolicy::Sequential;
    bool ptpEnabled = false;
    bool autoReconnect = true;
    int reconnectFailureThreshold = 3;
    int reconnectInitialDelayMs = 250;
    int reconnectMaxDelayMs = 5000;
};

struct HardwareOutputConnectionConfig
{
    bool enabled = true;
    HardwareOutputAdapterType adapterType = HardwareOutputAdapterType::ModbusTcp;
    DeviceEndpoint endpoint;
    HardwareOutputBinding binding;
    bool plcUseHoldingRegister = false;
    bool autoPublish = false;
    int maxQueueSize = 32;
    int retryCount = 2;
    int retryDelayMs = 150;
    bool reconnectBeforeRetry = true;
    HardwareHandshakeConfig handshake;
};

struct HardwareAuxiliaryOutputSnapshot
{
    std::string adapterKey;
    std::string adapterName;
    DeviceConnectionState state = DeviceConnectionState::Disconnected;
    DeviceOperationResult lastOperation;
};

struct CameraDiscoveryResult
{
    DeviceOperationResult operation;
    std::vector<CameraDeviceInfo> devices;
};

struct HardwareRuntimeSnapshot
{
    DeviceConnectionState cameraState = DeviceConnectionState::Disconnected;
    DeviceConnectionState outputState = DeviceConnectionState::Disconnected;
    std::string cameraAdapterName;
    std::string outputAdapterName;
    std::string outputAdapterKey;
    bool cameraAutoCapture = false;
    bool cameraCapturePending = false;
    bool cameraToolRunPending = false;
    bool cameraTriggerOnInspection = true;
    bool cameraReconnecting = false;
    bool outputAutoPublish = false;
    bool outputQueueBusy = false;
    bool outputReconnecting = false;
    bool outputCommunicationAlarm = false;
    bool handshakeEnabled = false;
    bool handshakeActive = false;
    bool handshakeAwaitingAcknowledge = false;
    bool handshakeTestActive = false;
    bool handshakeAlarm = false;
    int cameraFrameIndex = 0;
    int cameraSlotIndex = -1;
    int cameraConsecutiveFailures = 0;
    int cameraReconnectAttempts = 0;
    int cameraReconnectDelayMs = 0;
    int outputConsecutiveFailures = 0;
    int outputReconnectAttempts = 0;
    int outputReconnectDelayMs = 0;
    std::size_t outputQueueDepth = 0;
    std::uint64_t outputSentCount = 0;
    std::uint64_t outputFailedCount = 0;
    std::uint64_t outputDroppedCount = 0;
    std::uint64_t handshakeIgnoredTriggerCount = 0;
    double outputLastCommunicationTimestampMs = 0.0;
    std::string handshakeTaskGroupName;
    std::string handshakeAlarmMessage;
    CameraTriggerConfig cameraTrigger;
    CameraFrameMetadata cameraFrameMetadata;
    CameraCapabilities cameraCapabilities;
    CameraStatistics cameraStatistics;
    DeviceOperationResult lastCameraOperation;
    DeviceOperationResult lastOutputOperation;
};

namespace HardwareRuntimeService
{
    DeviceOperationResult ConnectCamera(const HardwareCameraConnectionConfig& config);
    DeviceOperationResult ActivateCameraSlot(int cameraIndex);
    DeviceOperationResult StartCameraCapture(const HardwareCameraConnectionConfig& config);
    void DisconnectCamera();
    void SetCameraAutoCapture(bool enabled);
    bool CameraAutoCaptureEnabled();
    void SetCameraTriggerOnInspection(bool enabled);
    bool CameraTriggerOnInspectionEnabled();
    void SetCameraOrientation(int orientation);
    DeviceOperationResult SetCameraControl(CameraControl control, double value);
    DeviceOperationResult ConfigureCameraTrigger(const CameraTriggerConfig& config);
    DeviceOperationResult ConfigureCameraBufferPolicy(CameraBufferPolicy policy);
    DeviceOperationResult ExecuteCameraSoftwareTrigger();
    DeviceOperationResult ConfigureCameraPtp(bool enabled);
    CameraDiscoveryResult DiscoverCameras(const std::string& backend);
    DeviceOperationResult ForceCameraIp(const std::string& backend,
        const std::string& selector, const std::string& ipAddress,
        const std::string& subnetMask, const std::string& defaultGateway);
    void RequestCameraFrame(bool runToolChainAfterCapture = false, bool loop = false);
    void CancelPendingCameraToolRun();

    DeviceOperationResult ConnectOutput(const HardwareOutputConnectionConfig& config);
    void DisconnectOutput();
    DeviceOperationResult ConnectAuxiliaryOutput(
        const HardwareOutputConnectionConfig& config);
    void ConfigureAuxiliaryOutputBinding(
        const HardwareOutputConnectionConfig& config);
    void DisconnectAuxiliaryOutput(const std::string& adapterKey);
    std::vector<HardwareAuxiliaryOutputSnapshot> AuxiliaryOutputSnapshots();
    void ConfigureOutputBinding(const HardwareOutputBinding& binding, bool autoPublish);
    void ConfigureModbusHandshake(const HardwareHandshakeConfig& config);
    void SetOutputAutoPublish(bool enabled);
    bool OutputAutoPublishEnabled();

    DeviceOperationResult GrabCameraFrame(int timeoutMs = 1000,
        const std::string& sourceName = "camera", int frameIndex = -1,
        double timestampMs = 0.0);

    DeviceOperationResult PublishInspectionStatus(ToolResultStatus status,
        const HardwareOutputBinding& binding);
    DeviceOperationResult TestIoMapping(std::size_t mappingIndex, bool active);
    DeviceOperationResult RequestHandshakeTest(ToolResultStatus status = ToolResultStatus::Pass);
    DeviceOperationResult RequestTaskInspection(const std::string& taskGroupName,
        bool preferCamera = true); // 相机在线时抓新帧，否则使用任务文件夹/单图

    ToolResultStatus AggregateInspectionStatus(const std::vector<ToolResult>& results);
    DeviceOperationResult EnqueueConfiguredStatus(ToolResultStatus status);
    DeviceOperationResult PublishConfiguredStatus(ToolResultStatus status);
    DeviceOperationResult PublishInspectionResults(const std::vector<ToolResult>& results);
    bool WaitForOutputIdle(int timeoutMs = 3000);

    // Called from the UI thread once per frame. Completed camera grabs are published
    // to ImageState here so worker threads never touch rendering/application state.
    void Tick();
    HardwareRuntimeSnapshot Snapshot();
    void Shutdown();
}
