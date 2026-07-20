#include "HardwareRuntimeService.h"

#include "FrameSourceState.h"
#include "ImageState.h"
#include "ImageUtils.h"
#include "ModbusPlcAdapter.h"
#include "ModbusTcpAdapter.h"
#include "Open62541OpcUaAdapter.h"
#include "OpenCvCameraAdapter.h"
#include "TcpTextAdapter.h"
#include "ToolController.h"
#include "VideoCapture.h"

#include <opencv2/core/mat.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace
{
struct PendingCameraFrame
{
    DeviceOperationResult operation;
    cv::Mat frame;
    std::string sourceName;
    int frameIndex = -1;
    double timestampMs = 0.0;
};

HardwareCameraConnectionConfig s_cameraConfig;
HardwareOutputConnectionConfig s_outputConfig;
HardwareOutputBinding s_outputBinding;
std::string s_outputAdapterKey;
std::thread s_cameraWorker;
std::mutex s_cameraWorkerMutex;
std::condition_variable s_cameraWorkerCondition;
PendingCameraFrame s_pendingCameraFrame;
bool s_hasPendingCameraFrame = false;
bool s_cameraWorkerStop = false;
bool s_cameraWorkerBusy = false;
bool s_cameraFrameRequested = false;
bool s_cameraAutoCapture = false;
bool s_cameraTriggerOnInspection = true;
int s_runToolChainAfterFrameIndex = -1;
bool s_cameraToolRunPending = false;
bool s_cameraToolRunLoop = false;
bool s_outputAutoPublish = false;
int s_cameraFrameIndex = 0;
int s_cameraScheduledFrameIndex = 0;
DeviceOperationResult s_lastCameraOperation;
DeviceOperationResult s_lastOutputOperation;

DeviceOperationResult NotConnected(const char* name)
{
    return {false, std::string(name) + " 未连接"};
}

void PublishFrame(const cv::Mat& frame, const std::string& sourceName,
    int frameIndex, double timestampMs)
{
    FrameSourceState::SetCurrentFrame(frame, FrameSourceType::Camera,
        sourceName, frameIndex, timestampMs);

    cv::Mat rgba;
    SafeConvertToRGBA(frame, rgba);
    if (!rgba.empty())
    {
        ImageState::PendingUploadRef() = std::move(rgba);
        ImageState::NeedUploadRef() = true;
    }
}

double CurrentTimestampMs()
{
    return std::chrono::duration<double, std::milli>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void CameraWorkerLoop(ICameraAdapter* camera)
{
    std::unique_lock<std::mutex> lock(s_cameraWorkerMutex);
    while (!s_cameraWorkerStop)
    {
        if (!s_cameraFrameRequested)
        {
            if (s_cameraAutoCapture)
            {
                s_cameraWorkerCondition.wait_for(lock,
                    std::chrono::milliseconds((std::max)(1,
                        s_cameraConfig.captureIntervalMs)),
                    []
                    {
                        return s_cameraWorkerStop || s_cameraFrameRequested ||
                            !s_cameraAutoCapture;
                    });
            }
            else
            {
                s_cameraWorkerCondition.wait(lock, []
                {
                    return s_cameraWorkerStop || s_cameraFrameRequested ||
                        s_cameraAutoCapture;
                });
            }
        }

        if (s_cameraWorkerStop)
            break;
        if (!s_cameraFrameRequested && !s_cameraAutoCapture)
            continue;

        s_cameraFrameRequested = false;
        s_cameraWorkerBusy = true;
        const int timeoutMs = (std::max)(1, s_cameraConfig.grabTimeoutMs);
        const int frameIndex = ++s_cameraScheduledFrameIndex;
        const std::string sourceName = s_cameraConfig.sourceName.empty()
            ? camera->AdapterName()
            : s_cameraConfig.sourceName;
        const double timestampMs = CurrentTimestampMs();
        lock.unlock();

        PendingCameraFrame pending;
        pending.sourceName = sourceName;
        pending.frameIndex = frameIndex;
        pending.timestampMs = timestampMs;
        pending.operation = camera->GrabFrame(pending.frame, timeoutMs);

        lock.lock();
        s_cameraWorkerBusy = false;
        s_pendingCameraFrame = std::move(pending);
        s_hasPendingCameraFrame = true;
    }
}

void StopCameraWorker()
{
    {
        std::lock_guard<std::mutex> lock(s_cameraWorkerMutex);
        s_cameraWorkerStop = true;
        s_cameraAutoCapture = false;
        s_cameraTriggerOnInspection = true;
        s_cameraFrameRequested = false;
        s_runToolChainAfterFrameIndex = -1;
        s_cameraToolRunPending = false;
        s_cameraToolRunLoop = false;
    }
    s_cameraWorkerCondition.notify_all();
    if (s_cameraWorker.joinable())
        s_cameraWorker.join();

    std::lock_guard<std::mutex> lock(s_cameraWorkerMutex);
    if (s_hasPendingCameraFrame)
        s_lastCameraOperation = s_pendingCameraFrame.operation;
    s_pendingCameraFrame = {};
    s_hasPendingCameraFrame = false;
    s_cameraWorkerBusy = false;
    s_cameraWorkerStop = false;
}

void StartCameraWorker(ICameraAdapter* camera)
{
    std::lock_guard<std::mutex> lock(s_cameraWorkerMutex);
    s_cameraWorkerStop = false;
    s_cameraWorkerBusy = false;
    s_hasPendingCameraFrame = false;
    s_cameraFrameRequested = true;
    s_cameraScheduledFrameIndex = 0;
    s_cameraWorker = std::thread(CameraWorkerLoop, camera);
}

DeviceOperationResult InvalidConfiguration(std::string message)
{
    return {false, std::move(message)};
}
}

namespace HardwareRuntimeService
{
DeviceOperationResult ConnectCamera(const HardwareCameraConnectionConfig& rawConfig)
{
    StopCameraWorker();

    HardwareCameraConnectionConfig config = rawConfig;
    config.grabTimeoutMs = (std::max)(1, config.grabTimeoutMs);
    config.captureIntervalMs = (std::max)(1, config.captureIntervalMs);
    if (config.endpoint.address.empty())
        return s_lastCameraOperation = InvalidConfiguration("工业相机地址为空");

    auto camera = std::make_unique<OpenCvCameraAdapter>();
    DeviceOperationResult result = camera->Connect(config.endpoint);
    if (!result.success)
        return s_lastCameraOperation = std::move(result);

    VideoCapture::Close();
    HardwareAdapterService::SetCamera(std::move(camera));
    result = StartCameraCapture(config);
    if (!result.success)
        HardwareAdapterService::SetCamera({});
    return result;
}

DeviceOperationResult StartCameraCapture(const HardwareCameraConnectionConfig& rawConfig)
{
    StopCameraWorker();
    ICameraAdapter* camera = HardwareAdapterService::Camera();
    if (!camera)
        return s_lastCameraOperation = InvalidConfiguration("未注册工业相机适配器");
    if (camera->ConnectionState() != DeviceConnectionState::Connected)
        return s_lastCameraOperation = NotConnected(camera->AdapterName());

    HardwareCameraConnectionConfig config = rawConfig;
    config.grabTimeoutMs = (std::max)(1, config.grabTimeoutMs);
    config.captureIntervalMs = (std::max)(1, config.captureIntervalMs);
    DeviceOperationResult result = camera->StartStream();
    if (!result.success)
        return s_lastCameraOperation = std::move(result);

    const DeviceOperationResult autoExposure = camera->SetControl(
        CameraControl::AutoExposure, config.autoExposure ? 1.0 : 0.0);
    if (!config.autoExposure)
    {
        camera->SetControl(CameraControl::Exposure, config.exposure);
        camera->SetControl(CameraControl::Gain, config.gain);
    }
    else
    {
        camera->SetControl(CameraControl::Gain, config.gain);
    }
    if (!autoExposure.success)
        s_lastCameraOperation = autoExposure;

    s_cameraConfig = std::move(config);
    s_cameraFrameIndex = 0;
    {
        std::lock_guard<std::mutex> lock(s_cameraWorkerMutex);
        s_cameraAutoCapture = s_cameraConfig.autoCapture;
        s_cameraTriggerOnInspection = s_cameraConfig.triggerOnInspection;
    }
    StartCameraWorker(camera);
    s_lastCameraOperation = {true, "工业相机已连接"};
    return s_lastCameraOperation;
}

void DisconnectCamera()
{
    StopCameraWorker();
    HardwareAdapterService::SetCamera({});
    s_lastCameraOperation = {true, "工业相机已断开"};
}

void SetCameraAutoCapture(bool enabled)
{
    {
        std::lock_guard<std::mutex> lock(s_cameraWorkerMutex);
        s_cameraAutoCapture = enabled;
        if (enabled)
            s_cameraFrameRequested = true;
    }
    s_cameraWorkerCondition.notify_all();
}

bool CameraAutoCaptureEnabled()
{
    std::lock_guard<std::mutex> lock(s_cameraWorkerMutex);
    return s_cameraAutoCapture;
}

void SetCameraTriggerOnInspection(bool enabled)
{
    std::lock_guard<std::mutex> lock(s_cameraWorkerMutex);
    s_cameraTriggerOnInspection = enabled;
}

bool CameraTriggerOnInspectionEnabled()
{
    std::lock_guard<std::mutex> lock(s_cameraWorkerMutex);
    return s_cameraTriggerOnInspection;
}

DeviceOperationResult SetCameraControl(CameraControl control, double value)
{
    ICameraAdapter* camera = HardwareAdapterService::Camera();
    if (!camera)
        return {false, "industrial camera adapter is not connected"};
    return camera->SetControl(control, value);
}

void RequestCameraFrame(bool runToolChainAfterCapture, bool loop)
{
    {
        std::lock_guard<std::mutex> lock(s_cameraWorkerMutex);
        s_cameraFrameRequested = true;
        if (runToolChainAfterCapture)
        {
            s_runToolChainAfterFrameIndex = s_cameraScheduledFrameIndex + 1;
            s_cameraToolRunLoop = loop;
        }
    }
    s_cameraWorkerCondition.notify_all();
}

DeviceOperationResult ConnectOutput(const HardwareOutputConnectionConfig& rawConfig)
{
    HardwareOutputConnectionConfig config = rawConfig;
    if (config.binding.adapterKey.empty())
        return s_lastOutputOperation = InvalidConfiguration("设备适配器标识为空");
    if (config.endpoint.address.empty())
        return s_lastOutputOperation = InvalidConfiguration("设备地址为空");

    std::unique_ptr<IDeviceAdapter> adapter;
    switch (config.adapterType)
    {
    case HardwareOutputAdapterType::ModbusTcp:
        config.binding.kind = HardwareOutputKind::ModbusCoil;
        adapter = std::make_unique<ModbusTcpAdapter>();
        break;
    case HardwareOutputAdapterType::ModbusPlc:
    {
        if (config.binding.target.empty())
            return s_lastOutputOperation = InvalidConfiguration("PLC 标签为空");
        auto plc = std::make_unique<ModbusPlcAdapter>();
        ModbusPlcTagBinding tagBinding;
        tagBinding.kind = config.plcUseHoldingRegister
            ? ModbusPlcTagKind::HoldingRegister
            : ModbusPlcTagKind::Coil;
        tagBinding.valueType = ModbusPlcValueType::Boolean;
        tagBinding.address = config.binding.address;
        if (!plc->ConfigureTag(config.binding.target, tagBinding))
            return s_lastOutputOperation = InvalidConfiguration("PLC 标签映射无效");
        config.binding.kind = HardwareOutputKind::PlcTag;
        adapter = std::move(plc);
        break;
    }
    case HardwareOutputAdapterType::OpcUa:
        if (config.binding.target.empty())
            return s_lastOutputOperation = InvalidConfiguration("OPC UA NodeId 为空");
        config.binding.kind = HardwareOutputKind::OpcUaNode;
        adapter = std::make_unique<Open62541OpcUaAdapter>();
        break;
    case HardwareOutputAdapterType::TcpText:
        config.binding.kind = HardwareOutputKind::TcpText;
        adapter = std::make_unique<TcpTextAdapter>();
        break;
    }

    DeviceOperationResult result = adapter->Connect(config.endpoint);
    if (!result.success)
        return s_lastOutputOperation = std::move(result);

    const std::string key = config.binding.adapterKey;
    HardwareAdapterService::Remove(key);
    if (!HardwareAdapterService::Register(key, std::move(adapter)))
        return s_lastOutputOperation = InvalidConfiguration("设备适配器注册失败: " + key);

    if (!s_outputAdapterKey.empty() && s_outputAdapterKey != key)
        HardwareAdapterService::Remove(s_outputAdapterKey);
    s_outputConfig = std::move(config);
    s_outputBinding = s_outputConfig.binding;
    s_outputAdapterKey = key;
    s_outputAutoPublish = s_outputConfig.autoPublish;
    s_lastOutputOperation = {true, result.message.empty()
        ? "硬件输出已连接"
        : std::move(result.message)};
    return s_lastOutputOperation;
}

void DisconnectOutput()
{
    if (!s_outputAdapterKey.empty())
        HardwareAdapterService::Remove(s_outputAdapterKey);
    s_outputAdapterKey.clear();
    s_outputAutoPublish = false;
    s_lastOutputOperation = {true, "硬件输出已断开"};
}

void ConfigureOutputBinding(const HardwareOutputBinding& binding, bool autoPublish)
{
    s_outputBinding = binding;
    s_outputAdapterKey = binding.adapterKey;
    s_outputAutoPublish = autoPublish;
}

void SetOutputAutoPublish(bool enabled)
{
    s_outputAutoPublish = enabled;
}

bool OutputAutoPublishEnabled()
{
    return s_outputAutoPublish;
}

DeviceOperationResult GrabCameraFrame(int timeoutMs, const std::string& sourceName,
    int frameIndex, double timestampMs)
{
    ICameraAdapter* camera = HardwareAdapterService::Camera();
    if (!camera)
        return {false, "未注册工业相机适配器"};
    if (camera->ConnectionState() != DeviceConnectionState::Connected)
        return NotConnected(camera->AdapterName());

    cv::Mat frame;
    DeviceOperationResult result = camera->GrabFrame(frame, (std::max)(1, timeoutMs));
    if (!result.success)
        return result;
    if (frame.empty())
        return {false, "工业相机返回空帧"};

    PublishFrame(frame, sourceName.empty() ? camera->AdapterName() : sourceName,
        frameIndex, timestampMs);
    return {true, "工业相机帧已发布"};
}

DeviceOperationResult PublishInspectionStatus(ToolResultStatus status,
    const HardwareOutputBinding& binding)
{
    IDeviceAdapter* adapter = HardwareAdapterService::Find(binding.adapterKey);
    if (!adapter)
        return {false, "未找到设备适配器: " + binding.adapterKey};
    if (adapter->ConnectionState() != DeviceConnectionState::Connected)
        return NotConnected(adapter->AdapterName());

    bool pass = status == ToolResultStatus::Pass;
    if (binding.invert)
        pass = !pass;

    switch (binding.kind)
    {
    case HardwareOutputKind::PlcTag:
    {
        if (binding.target.empty())
            return {false, "PLC 输出标签为空"};
        auto* plc = dynamic_cast<IPlcAdapter*>(adapter);
        if (!plc)
            return {false, "设备适配器不支持 PLC 标签写入"};
        return plc->WriteTag(binding.target, DeviceValue(pass));
    }
    case HardwareOutputKind::ModbusCoil:
    {
        auto* modbus = dynamic_cast<IModbusTcpAdapter*>(adapter);
        if (!modbus)
            return {false, "设备适配器不支持 Modbus TCP 线圈写入"};
        return modbus->WriteCoil(binding.address, pass);
    }
    case HardwareOutputKind::OpcUaNode:
    {
        if (binding.target.empty())
            return {false, "OPC UA 输出节点为空"};
        auto* opcUa = dynamic_cast<IOpcUaAdapter*>(adapter);
        if (!opcUa)
            return {false, "设备适配器不支持 OPC UA 节点写入"};
        return opcUa->WriteNode(binding.target, DeviceValue(pass));
    }
    case HardwareOutputKind::TcpText:
    {
        auto* tcpText = dynamic_cast<ITcpTextAdapter*>(adapter);
        if (!tcpText)
            return {false, "设备适配器不支持 TCP 文本发送"};
        std::string payload = pass ? binding.passText : binding.failText;
        if (binding.appendCrLf)
            payload += "\r\n";
        return tcpText->SendText(payload);
    }
    }

    return {false, "不支持的硬件输出类型"};
}

ToolResultStatus AggregateInspectionStatus(const std::vector<ToolResult>& results)
{
    bool hasResult = false;
    ToolResultStatus aggregate = ToolResultStatus::Pass;
    for (const ToolResult& result : results)
    {
        if (result.skipped)
            continue;
        hasResult = true;
        if (result.status == ToolResultStatus::Error)
            return ToolResultStatus::Error;
        if (result.status == ToolResultStatus::Fail)
            aggregate = ToolResultStatus::Fail;
    }
    return hasResult ? aggregate : ToolResultStatus::Error;
}

DeviceOperationResult PublishConfiguredStatus(ToolResultStatus status)
{
    s_lastOutputOperation = PublishInspectionStatus(status, s_outputBinding);
    return s_lastOutputOperation;
}

DeviceOperationResult PublishInspectionResults(const std::vector<ToolResult>& results)
{
    return PublishConfiguredStatus(AggregateInspectionStatus(results));
}

void Tick()
{
    PendingCameraFrame pending;
    bool hasPendingFrame = false;
    bool runToolChainAfterPublish = false;
    bool runToolChainLoop = false;
    {
        std::lock_guard<std::mutex> lock(s_cameraWorkerMutex);
        if (s_hasPendingCameraFrame)
        {
            pending = std::move(s_pendingCameraFrame);
            s_pendingCameraFrame = {};
            s_hasPendingCameraFrame = false;
            hasPendingFrame = true;
            if (s_runToolChainAfterFrameIndex >= 0 &&
                pending.frameIndex >= s_runToolChainAfterFrameIndex)
            {
                runToolChainAfterPublish = pending.operation.success &&
                    !pending.frame.empty();
                runToolChainLoop = s_cameraToolRunLoop;
                s_runToolChainAfterFrameIndex = -1;
                s_cameraToolRunLoop = false;
            }
        }
    }
    if (hasPendingFrame)
    {
        s_lastCameraOperation = pending.operation;
        if (pending.operation.success && !pending.frame.empty())
        {
            PublishFrame(pending.frame, pending.sourceName,
                pending.frameIndex, pending.timestampMs);
            s_cameraFrameIndex = pending.frameIndex;
            s_lastCameraOperation.message = "工业相机帧已发布";
            if (runToolChainAfterPublish)
            {
                std::lock_guard<std::mutex> lock(s_cameraWorkerMutex);
                s_cameraToolRunPending = true;
                s_cameraToolRunLoop = runToolChainLoop;
            }
        }
    }

    bool requestToolRun = false;
    if (ToolController::GetMode() == ToolController::Mode::Idle)
    {
        std::lock_guard<std::mutex> lock(s_cameraWorkerMutex);
        if (s_cameraToolRunPending)
        {
            s_cameraToolRunPending = false;
            requestToolRun = true;
        }
    }
    if (requestToolRun)
    {
        bool loop = false;
        {
            std::lock_guard<std::mutex> lock(s_cameraWorkerMutex);
            loop = s_cameraToolRunLoop;
            s_cameraToolRunLoop = false;
        }
        ToolController::RequestRunAll(loop, false);
        s_lastCameraOperation.message = "工业相机帧已发布，工具链已开始执行";
    }
}

HardwareRuntimeSnapshot Snapshot()
{
    HardwareRuntimeSnapshot snapshot;
    if (const ICameraAdapter* camera = HardwareAdapterService::CameraReadOnly())
    {
        snapshot.cameraState = camera->ConnectionState();
        snapshot.cameraAdapterName = camera->AdapterName();
    }
    if (!s_outputAdapterKey.empty())
    {
        if (const IDeviceAdapter* output = HardwareAdapterService::FindReadOnly(s_outputAdapterKey))
        {
            snapshot.outputState = output->ConnectionState();
            snapshot.outputAdapterName = output->AdapterName();
        }
    }
    snapshot.outputAdapterKey = s_outputAdapterKey;
    {
        std::lock_guard<std::mutex> lock(s_cameraWorkerMutex);
        snapshot.cameraAutoCapture = s_cameraAutoCapture;
        snapshot.cameraTriggerOnInspection = s_cameraTriggerOnInspection;
        snapshot.cameraCapturePending = s_cameraWorkerBusy || s_hasPendingCameraFrame;
        snapshot.cameraToolRunPending = s_runToolChainAfterFrameIndex >= 0 ||
            s_cameraToolRunPending;
    }
    snapshot.outputAutoPublish = s_outputAutoPublish;
    snapshot.cameraFrameIndex = s_cameraFrameIndex;
    snapshot.lastCameraOperation = s_lastCameraOperation;
    snapshot.lastOutputOperation = s_lastOutputOperation;
    return snapshot;
}

void Shutdown()
{
    StopCameraWorker();
    HardwareAdapterService::Clear();
    s_outputAdapterKey.clear();
    s_outputAutoPublish = false;
}
}
