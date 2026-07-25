#pragma once

#include "HardwareAdapters.h"
#include "../Algorithm/ToolResult.h"

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

struct HardwareCameraConnectionConfig
{
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
    bool autoReconnect = true;
    int reconnectFailureThreshold = 3;
    int reconnectInitialDelayMs = 250;
    int reconnectMaxDelayMs = 5000;
};

struct HardwareOutputConnectionConfig
{
    HardwareOutputAdapterType adapterType = HardwareOutputAdapterType::ModbusTcp;
    DeviceEndpoint endpoint;
    HardwareOutputBinding binding;
    bool plcUseHoldingRegister = false;
    bool autoPublish = false;
    int maxQueueSize = 32;
    int retryCount = 2;
    int retryDelayMs = 150;
    bool reconnectBeforeRetry = true;
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
    int cameraFrameIndex = 0;
    int cameraConsecutiveFailures = 0;
    int cameraReconnectAttempts = 0;
    int cameraReconnectDelayMs = 0;
    std::size_t outputQueueDepth = 0;
    std::uint64_t outputSentCount = 0;
    std::uint64_t outputFailedCount = 0;
    std::uint64_t outputDroppedCount = 0;
    DeviceOperationResult lastCameraOperation;
    DeviceOperationResult lastOutputOperation;
};

namespace HardwareRuntimeService
{
    DeviceOperationResult ConnectCamera(const HardwareCameraConnectionConfig& config);
    DeviceOperationResult StartCameraCapture(const HardwareCameraConnectionConfig& config);
    void DisconnectCamera();
    void SetCameraAutoCapture(bool enabled);
    bool CameraAutoCaptureEnabled();
    void SetCameraTriggerOnInspection(bool enabled);
    bool CameraTriggerOnInspectionEnabled();
    void SetCameraOrientation(int orientation);
    DeviceOperationResult SetCameraControl(CameraControl control, double value);
    void RequestCameraFrame(bool runToolChainAfterCapture = false, bool loop = false);
    void CancelPendingCameraToolRun();

    DeviceOperationResult ConnectOutput(const HardwareOutputConnectionConfig& config);
    void DisconnectOutput();
    void ConfigureOutputBinding(const HardwareOutputBinding& binding, bool autoPublish);
    void SetOutputAutoPublish(bool enabled);
    bool OutputAutoPublishEnabled();

    DeviceOperationResult GrabCameraFrame(int timeoutMs = 1000,
        const std::string& sourceName = "camera", int frameIndex = -1,
        double timestampMs = 0.0);

    DeviceOperationResult PublishInspectionStatus(ToolResultStatus status,
        const HardwareOutputBinding& binding);

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
