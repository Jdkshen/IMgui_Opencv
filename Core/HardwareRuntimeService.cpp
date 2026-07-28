#include "HardwareRuntimeService.h"

#include "FrameSourceState.h"
#include "FrameArchiveService.h"
#include "ImageState.h"
#include "ImageUtils.h"
#include "ModbusPlcAdapter.h"
#include "ModbusTcpAdapter.h"
#include "Open62541OpcUaAdapter.h"
#include "OpenCvCameraAdapter.h"
#include "TcpTextAdapter.h"
#include "ToolController.h"
#include "ToolChainState.h"
#include "VideoCapture.h"

#include <opencv2/core/mat.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
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
std::atomic<int> s_cameraOrientation{0};
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
bool s_cameraToolRunFrameAvailable = false;
bool s_outputAutoPublish = false;
int s_cameraFrameIndex = 0;
int s_cameraScheduledFrameIndex = 0;
DeviceOperationResult s_lastCameraOperation;
DeviceOperationResult s_lastOutputOperation;
int s_cameraConsecutiveFailures = 0;
int s_cameraReconnectAttempts = 0;
int s_cameraReconnectDelayMs = 0;
bool s_cameraReconnecting = false;

struct PendingOutput
{
    ToolResultStatus status = ToolResultStatus::Error;
    HardwareOutputBinding binding;
    std::uint64_t sequence = 0;
};

std::thread s_outputWorker;
std::mutex s_outputWorkerMutex;
std::mutex s_outputAdapterMutex;
std::condition_variable s_outputWorkerCondition;
std::deque<PendingOutput> s_outputQueue;
bool s_outputWorkerStop = false;
bool s_outputWorkerBusy = false;
std::uint64_t s_outputSequence = 0;
std::uint64_t s_outputSentCount = 0;
std::uint64_t s_outputFailedCount = 0;
std::uint64_t s_outputDroppedCount = 0;

struct PendingIoWrite
{
    HardwareIoMapping mapping;
    bool active = false;
    bool usePulse = true;
};

struct ActiveIoPulse
{
    HardwareIoMapping mapping;
    std::chrono::steady_clock::time_point resetAt;
};

struct PendingTaskInspection
{
    std::string taskGroupName;
    bool preferCamera = true;
};

std::deque<PendingIoWrite> s_ioWriteQueue;
std::vector<ActiveIoPulse> s_activeIoPulses;
std::vector<bool> s_inputStates;
std::vector<bool> s_inputStateInitialized;
std::optional<PendingTaskInspection> s_pendingTaskInspection;
std::uint64_t s_handshakeIgnoredTriggerCount = 0;
bool s_handshakeActive = false;
bool s_handshakeAwaitingAcknowledge = false;
bool s_handshakeTestActive = false;
bool s_handshakeTestRequested = false;
bool s_handshakeAcknowledgeReceived = false;
std::string s_handshakeTaskGroupName;
ToolResultStatus s_handshakeTestStatus = ToolResultStatus::Pass;
std::uint64_t s_handshakeStartBatchSerial = 0;
std::chrono::steady_clock::time_point s_handshakeStartedAt;
std::chrono::steady_clock::time_point s_handshakeCompletedAt;
std::chrono::steady_clock::time_point s_handshakeTestCompleteAt;
std::chrono::steady_clock::time_point s_nextIoPollAt;
std::chrono::steady_clock::time_point s_nextHeartbeatAt;
std::chrono::steady_clock::time_point s_nextOutputReconnectAt;
bool s_heartbeatState = false;
int s_outputConsecutiveFailures = 0;
int s_outputReconnectAttempts = 0;
int s_outputReconnectDelayMs = 0;
bool s_outputReconnecting = false;
bool s_outputCommunicationAlarm = false;
double s_outputLastCommunicationTimestampMs = 0.0;
bool s_handshakeAlarm = false;
std::string s_handshakeAlarmMessage;

void ResetHandshakeStateLocked(bool resetIgnoredTriggerCount)
{
    s_pendingTaskInspection.reset();
    s_handshakeActive = false;
    s_handshakeAwaitingAcknowledge = false;
    s_handshakeTestActive = false;
    s_handshakeTestRequested = false;
    s_handshakeAcknowledgeReceived = false;
    s_handshakeTaskGroupName.clear();
    s_handshakeAlarm = false;
    s_handshakeAlarmMessage.clear();
    if (resetIgnoredTriggerCount)
        s_handshakeIgnoredTriggerCount = 0;
}

DeviceOperationResult NotConnected(const char* name)
{
    return {false, std::string(name) + " 未连接"};
}

void PublishFrame(const cv::Mat& frame, const std::string& sourceName,
    int frameIndex, double timestampMs)
{
    cv::Mat orientedFrame;
    switch (s_cameraOrientation.load())
    {
    case 1:
        cv::rotate(frame, orientedFrame, cv::ROTATE_90_CLOCKWISE);
        break;
    case 2:
        cv::rotate(frame, orientedFrame, cv::ROTATE_180);
        break;
    case 3:
        cv::rotate(frame, orientedFrame, cv::ROTATE_90_COUNTERCLOCKWISE);
        break;
    case 4:
        cv::flip(frame, orientedFrame, 1);
        break;
    case 5:
        cv::flip(frame, orientedFrame, 0);
        break;
    default:
        break;
    }
    const cv::Mat& outputFrame = orientedFrame.empty() ? frame : orientedFrame;

    FrameArchiveService::Enqueue(outputFrame, sourceName, frameIndex, timestampMs);
    FrameSourceState::SetCurrentFrame(outputFrame, FrameSourceType::Camera,
        sourceName, frameIndex, timestampMs);

    cv::Mat rgba;
    SafeConvertToRGBA(outputFrame, rgba);
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

DeviceOperationResult ApplyCameraControls(
    ICameraAdapter* camera, const HardwareCameraConnectionConfig& config)
{
    DeviceOperationResult result = camera->SetControl(
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
    return result;
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
        if (pending.operation.success && !pending.frame.empty())
        {
            s_cameraConsecutiveFailures = 0;
            s_cameraReconnectDelayMs = 0;
            s_cameraReconnecting = false;
        }
        else
        {
            ++s_cameraConsecutiveFailures;
            const int failureThreshold = (std::max)(1,
                s_cameraConfig.reconnectFailureThreshold);
            if (s_cameraConfig.autoReconnect &&
                s_cameraConsecutiveFailures >= failureThreshold)
            {
                const int initialDelay = (std::max)(1,
                    s_cameraConfig.reconnectInitialDelayMs);
                const int maximumDelay = (std::max)(initialDelay,
                    s_cameraConfig.reconnectMaxDelayMs);
                s_cameraReconnectDelayMs = s_cameraReconnectDelayMs <= 0
                    ? initialDelay
                    : (std::min)(maximumDelay, s_cameraReconnectDelayMs * 2);
                const int reconnectDelay = s_cameraReconnectDelayMs;
                const HardwareCameraConnectionConfig reconnectConfig = s_cameraConfig;
                s_cameraReconnecting = true;
                ++s_cameraReconnectAttempts;

                if (s_hasPendingCameraFrame)
                    s_lastCameraOperation = s_pendingCameraFrame.operation;
                s_pendingCameraFrame = std::move(pending);
                s_hasPendingCameraFrame = true;
                s_cameraWorkerBusy = false;

                const bool stopped = s_cameraWorkerCondition.wait_for(lock,
                    std::chrono::milliseconds(reconnectDelay), []
                    {
                        return s_cameraWorkerStop;
                    });
                if (stopped)
                    break;

                lock.unlock();
                camera->StopStream();
                camera->Disconnect();
                DeviceOperationResult reconnect = camera->Connect(reconnectConfig.endpoint);
                if (reconnect.success)
                    reconnect = camera->StartStream();
                if (reconnect.success)
                    ApplyCameraControls(camera, reconnectConfig);
                lock.lock();

                s_cameraReconnecting = false;
                if (reconnect.success)
                {
                    s_cameraConsecutiveFailures = 0;
                    s_cameraReconnectDelayMs = 0;
                    s_lastCameraOperation = {true, "工业相机已自动重连"};
                    s_cameraFrameRequested = true;
                }
                else
                {
                    s_lastCameraOperation = reconnect;
                }
                continue;
            }
        }
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
        s_cameraToolRunFrameAvailable = false;
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
    s_cameraReconnecting = false;
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

DeviceOperationResult ValidateHandshakeConfig(const HardwareHandshakeConfig& config)
{
    if (!config.enabled)
        return {true, {}};
    bool hasTrigger = false;
    bool hasBusy = false;
    bool hasDone = false;
    bool hasResult = false;
    std::set<std::uint16_t> addresses;
    for (const HardwareIoMapping& mapping : config.mappings)
    {
        if (!mapping.enabled)
            continue;
        if (!addresses.insert(mapping.address).second)
            return InvalidConfiguration("PLC IO 地址重复: " +
                std::to_string(mapping.address));
        const bool inputSignal = mapping.signal == HardwareIoSignal::Trigger ||
            mapping.signal == HardwareIoSignal::Acknowledge;
        const HardwareIoDirection expectedDirection = inputSignal
            ? HardwareIoDirection::Input : HardwareIoDirection::Output;
        if (mapping.direction != expectedDirection)
            return InvalidConfiguration("PLC IO 信号方向不正确，地址: " +
                std::to_string(mapping.address));
        if (mapping.signal == HardwareIoSignal::Trigger)
        {
            if (mapping.taskGroupName.empty())
                return InvalidConfiguration("Trigger 必须绑定任务");
            hasTrigger = true;
        }
        hasBusy |= mapping.signal == HardwareIoSignal::Busy;
        hasDone |= mapping.signal == HardwareIoSignal::Done;
        hasResult |= mapping.signal == HardwareIoSignal::Ok ||
            mapping.signal == HardwareIoSignal::Ng ||
            mapping.signal == HardwareIoSignal::Error;
    }
    if (!hasTrigger || !hasBusy || !hasDone || !hasResult)
    {
        return InvalidConfiguration(
            "PLC 握手至少需要 Trigger、Busy、Done 和一个结果输出");
    }
    return {true, {}};
}

DeviceOperationResult ReconnectOutputAdapter()
{
    std::lock_guard<std::mutex> adapterLock(s_outputAdapterMutex);
    IDeviceAdapter* adapter = HardwareAdapterService::Find(s_outputAdapterKey);
    if (!adapter)
        return {false, "未找到设备适配器: " + s_outputAdapterKey};
    adapter->Disconnect();
    return adapter->Connect(s_outputConfig.endpoint);
}

void QueueSignalLocked(HardwareIoSignal signal, bool active, bool usePulse = true)
{
    for (const HardwareIoMapping& mapping : s_outputConfig.handshake.mappings)
    {
        if (!mapping.enabled || mapping.direction != HardwareIoDirection::Output ||
            mapping.signal != signal)
        {
            continue;
        }
        s_ioWriteQueue.push_back({mapping, active, usePulse});
    }
    s_outputWorkerCondition.notify_one();
}

void QueueHandshakeStartLocked()
{
    s_handshakeAlarm = false;
    s_handshakeAlarmMessage.clear();
    QueueSignalLocked(HardwareIoSignal::Done, false, false);
    QueueSignalLocked(HardwareIoSignal::Ok, false, false);
    QueueSignalLocked(HardwareIoSignal::Ng, false, false);
    QueueSignalLocked(HardwareIoSignal::Error, false, false);
    QueueSignalLocked(HardwareIoSignal::Busy, true, false);
}

void QueueHandshakeCompleteLocked(ToolResultStatus status)
{
    QueueSignalLocked(HardwareIoSignal::Busy, false, false);
    QueueSignalLocked(HardwareIoSignal::Ok,
        status == ToolResultStatus::Pass, false);
    QueueSignalLocked(HardwareIoSignal::Ng,
        status == ToolResultStatus::Fail, false);
    QueueSignalLocked(HardwareIoSignal::Error,
        status == ToolResultStatus::Error, false);
    QueueSignalLocked(HardwareIoSignal::Done, true, true);
}

void QueueHandshakeResetLocked()
{
    QueueSignalLocked(HardwareIoSignal::Busy, false, false);
    QueueSignalLocked(HardwareIoSignal::Done, false, false);
    QueueSignalLocked(HardwareIoSignal::Ok, false, false);
    QueueSignalLocked(HardwareIoSignal::Ng, false, false);
    QueueSignalLocked(HardwareIoSignal::Error, false, false);
}

DeviceOperationResult WriteIoMapping(const HardwareIoMapping& mapping, bool active)
{
    std::lock_guard<std::mutex> adapterLock(s_outputAdapterMutex);
    IDeviceAdapter* adapter = HardwareAdapterService::Find(s_outputAdapterKey);
    if (!adapter)
        return {false, "未找到 Modbus TCP 适配器: " + s_outputAdapterKey};
    if (adapter->ConnectionState() != DeviceConnectionState::Connected)
        return NotConnected(adapter->AdapterName());
    auto* modbus = dynamic_cast<IModbusTcpAdapter*>(adapter);
    if (!modbus)
        return {false, "当前适配器不支持 Modbus TCP IO"};
    return modbus->WriteCoil(mapping.address, mapping.invert ? !active : active);
}

void RecordCommunicationResultLocked(const DeviceOperationResult& result)
{
    s_lastOutputOperation = result;
    if (result.success)
    {
        s_outputLastCommunicationTimestampMs = CurrentTimestampMs();
        s_outputConsecutiveFailures = 0;
        s_outputReconnectDelayMs = 0;
        s_outputReconnecting = false;
        s_outputCommunicationAlarm = false;
        return;
    }

    ++s_outputConsecutiveFailures;
    const int threshold = (std::max)(1,
        s_outputConfig.handshake.reconnectFailureThreshold);
    if (s_outputConsecutiveFailures >= threshold)
    {
        s_outputCommunicationAlarm = true;
        if (s_outputConfig.handshake.autoReconnect)
        {
            const int initialDelay = (std::max)(1,
                s_outputConfig.handshake.reconnectInitialDelayMs);
            const int maximumDelay = (std::max)(initialDelay,
                s_outputConfig.handshake.reconnectMaxDelayMs);
            s_outputReconnectDelayMs = s_outputReconnectDelayMs <= 0
                ? initialDelay
                : (std::min)(maximumDelay, s_outputReconnectDelayMs * 2);
            s_nextOutputReconnectAt = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(s_outputReconnectDelayMs);
            s_outputReconnecting = true;
        }
    }
}

DeviceOperationResult PollHandshakeInputs(
    const std::vector<std::pair<std::size_t, HardwareIoMapping>>& inputs,
    std::vector<bool>& logicalValues)
{
    logicalValues.clear();
    logicalValues.reserve(inputs.size());
    std::lock_guard<std::mutex> adapterLock(s_outputAdapterMutex);
    IDeviceAdapter* adapter = HardwareAdapterService::Find(s_outputAdapterKey);
    if (!adapter)
        return {false, "未找到 Modbus TCP 适配器: " + s_outputAdapterKey};
    if (adapter->ConnectionState() != DeviceConnectionState::Connected)
        return NotConnected(adapter->AdapterName());
    auto* modbus = dynamic_cast<IModbusTcpAdapter*>(adapter);
    if (!modbus)
        return {false, "当前适配器不支持 Modbus TCP IO 轮询"};

    const auto addressRange = std::minmax_element(inputs.begin(), inputs.end(),
        [](const auto& left, const auto& right)
        {
            return left.second.address < right.second.address;
        });
    const std::uint16_t firstAddress = addressRange.first->second.address;
    const std::uint16_t lastAddress = addressRange.second->second.address;
    const std::uint32_t span = static_cast<std::uint32_t>(lastAddress) -
        firstAddress + 1u;
    if (span <= 2000u)
    {
        std::vector<bool> values;
        DeviceOperationResult result = modbus->ReadCoils(firstAddress,
            static_cast<std::uint16_t>(span), values);
        if (!result.success || values.size() != span)
            return result.success
                ? DeviceOperationResult{false, "Modbus 输入线圈批量读取长度不匹配"}
                : result;
        for (const auto& input : inputs)
        {
            const bool physical = values[static_cast<std::size_t>(
                input.second.address - firstAddress)];
            logicalValues.push_back(input.second.invert ? !physical : physical);
        }
        return {true, "Modbus IO 批量轮询正常"};
    }

    for (const auto& input : inputs)
    {
        std::vector<bool> values;
        DeviceOperationResult result = modbus->ReadCoils(input.second.address, 1, values);
        if (!result.success || values.size() != 1)
            return result.success
                ? DeviceOperationResult{false, "Modbus 输入线圈返回为空"}
                : result;
        logicalValues.push_back(input.second.invert ? !values.front() : values.front());
    }
    return {true, "Modbus IO 轮询正常"};
}

bool HasAcknowledgeInputLocked()
{
    return std::any_of(s_outputConfig.handshake.mappings.begin(),
        s_outputConfig.handshake.mappings.end(),
        [](const HardwareIoMapping& mapping)
        {
            return mapping.enabled &&
                mapping.direction == HardwareIoDirection::Input &&
                mapping.signal == HardwareIoSignal::Acknowledge;
        });
}

void CompleteHandshakeLocked(ToolResultStatus status)
{
    QueueHandshakeCompleteLocked(status);
    s_handshakeCompletedAt = std::chrono::steady_clock::now();
    s_handshakeAwaitingAcknowledge = HasAcknowledgeInputLocked();
    if (!s_handshakeAwaitingAcknowledge)
    {
        s_handshakeActive = false;
        s_handshakeTestActive = false;
        s_handshakeTaskGroupName.clear();
    }
}

ToolResultStatus AggregateTaskGroupStatus(const std::string& taskGroupName)
{
    std::vector<ToolResult> results;
    for (const ToolInstance& tool : ToolChainState::ReadOnlyTools())
    {
        if (tool.groupName == taskGroupName && tool.hasLastResult)
            results.push_back(tool.lastResult);
    }
    return HardwareRuntimeService::AggregateInspectionStatus(results);
}

void OutputWorkerLoop()
{
    std::unique_lock<std::mutex> lock(s_outputWorkerMutex);
    while (!s_outputWorkerStop)
    {
        s_outputWorkerCondition.wait_for(lock, std::chrono::milliseconds(10), []
        {
            return s_outputWorkerStop || !s_outputQueue.empty() ||
                !s_ioWriteQueue.empty();
        });
        if (s_outputWorkerStop)
            break;

        const auto now = std::chrono::steady_clock::now();
        for (auto pulse = s_activeIoPulses.begin(); pulse != s_activeIoPulses.end();)
        {
            if (pulse->resetAt <= now)
            {
                s_ioWriteQueue.push_back({pulse->mapping, false, false});
                pulse = s_activeIoPulses.erase(pulse);
            }
            else
            {
                ++pulse;
            }
        }

        const bool reconnectNow = s_outputReconnecting &&
            now >= s_nextOutputReconnectAt;
        if (reconnectNow)
        {
            s_outputWorkerBusy = true;
            ++s_outputReconnectAttempts;
            lock.unlock();
            DeviceOperationResult result = ReconnectOutputAdapter();
            lock.lock();
            RecordCommunicationResultLocked(result);
            s_outputWorkerBusy = false;
            s_outputWorkerCondition.notify_all();
            continue;
        }

        if (!s_ioWriteQueue.empty())
        {
            PendingIoWrite write = std::move(s_ioWriteQueue.front());
            s_ioWriteQueue.pop_front();
            s_outputWorkerBusy = true;
            lock.unlock();
            DeviceOperationResult result = WriteIoMapping(write.mapping, write.active);
            lock.lock();
            RecordCommunicationResultLocked(result);
            if (result.success && write.active && write.usePulse &&
                write.mapping.pulseMs > 0)
            {
                s_activeIoPulses.push_back({write.mapping,
                    std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(write.mapping.pulseMs)});
            }
            s_outputWorkerBusy = false;
            s_outputWorkerCondition.notify_all();
            continue;
        }

        if (!s_outputQueue.empty())
        {
            PendingOutput pending = std::move(s_outputQueue.front());
            s_outputQueue.pop_front();
            s_outputWorkerBusy = true;
            const int retries = (std::max)(0, s_outputConfig.retryCount);
            const int retryDelayMs = (std::max)(1, s_outputConfig.retryDelayMs);
            const bool reconnectBeforeRetry = s_outputConfig.reconnectBeforeRetry;
            lock.unlock();

            DeviceOperationResult result;
            for (int attempt = 0; attempt <= retries; ++attempt)
            {
                result = HardwareRuntimeService::PublishInspectionStatus(
                    pending.status, pending.binding);
                if (result.success)
                    break;
                if (attempt < retries)
                {
                    if (reconnectBeforeRetry)
                        ReconnectOutputAdapter();
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(retryDelayMs));
                }
            }

            lock.lock();
            RecordCommunicationResultLocked(result);
            if (result.success)
                ++s_outputSentCount;
            else
                ++s_outputFailedCount;
            s_outputWorkerBusy = false;
            s_outputWorkerCondition.notify_all();
            continue;
        }

        if (!s_outputConfig.handshake.enabled)
            continue;

        if (now >= s_nextHeartbeatAt)
        {
            s_heartbeatState = !s_heartbeatState;
            QueueSignalLocked(HardwareIoSignal::Heartbeat,
                s_heartbeatState, false);
            s_nextHeartbeatAt = now + std::chrono::milliseconds((std::max)(100,
                s_outputConfig.handshake.heartbeatIntervalMs));
            continue;
        }

        if (now < s_nextIoPollAt)
            continue;

        std::vector<std::pair<std::size_t, HardwareIoMapping>> inputs;
        for (std::size_t index = 0;
            index < s_outputConfig.handshake.mappings.size(); ++index)
        {
            const HardwareIoMapping& mapping =
                s_outputConfig.handshake.mappings[index];
            if (mapping.enabled && mapping.direction == HardwareIoDirection::Input)
                inputs.emplace_back(index, mapping);
        }
        s_nextIoPollAt = now + std::chrono::milliseconds((std::max)(10,
            s_outputConfig.handshake.pollIntervalMs));
        if (inputs.empty())
            continue;

        lock.unlock();
        std::vector<bool> values;
        DeviceOperationResult pollResult = PollHandshakeInputs(inputs, values);
        lock.lock();
        RecordCommunicationResultLocked(pollResult);
        if (!pollResult.success)
            continue;

        if (s_inputStates.size() != s_outputConfig.handshake.mappings.size())
        {
            s_inputStates.assign(s_outputConfig.handshake.mappings.size(), false);
            s_inputStateInitialized.assign(
                s_outputConfig.handshake.mappings.size(), false);
        }
        for (std::size_t valueIndex = 0; valueIndex < inputs.size(); ++valueIndex)
        {
            const std::size_t mappingIndex = inputs[valueIndex].first;
            const bool value = values[valueIndex];
            const bool rising = s_inputStateInitialized[mappingIndex] &&
                !s_inputStates[mappingIndex] && value;
            s_inputStates[mappingIndex] = value;
            s_inputStateInitialized[mappingIndex] = true;
            if (!rising)
                continue;

            const HardwareIoMapping& mapping = inputs[valueIndex].second;
            if (mapping.signal == HardwareIoSignal::Trigger &&
                !mapping.taskGroupName.empty())
            {
                if (!s_handshakeActive && !s_handshakeTestRequested &&
                    !s_pendingTaskInspection.has_value())
                {
                    s_pendingTaskInspection =
                        PendingTaskInspection{mapping.taskGroupName, true};
                }
                else
                {
                    ++s_handshakeIgnoredTriggerCount;
                }
            }
            else if (mapping.signal == HardwareIoSignal::Acknowledge)
            {
                s_handshakeAcknowledgeReceived = true;
            }
        }
    }
}

void StartOutputWorker()
{
    std::lock_guard<std::mutex> lock(s_outputWorkerMutex);
    if (s_outputWorker.joinable())
        return;
    s_outputWorkerStop = false;
    const auto now = std::chrono::steady_clock::now();
    s_nextIoPollAt = now;
    s_nextHeartbeatAt = now;
    s_nextOutputReconnectAt = now;
    s_inputStates.assign(s_outputConfig.handshake.mappings.size(), false);
    s_inputStateInitialized.assign(
        s_outputConfig.handshake.mappings.size(), false);
    s_outputWorker = std::thread(OutputWorkerLoop);
}

void StopOutputWorker(bool clearQueue)
{
    {
        std::lock_guard<std::mutex> lock(s_outputWorkerMutex);
        s_outputWorkerStop = true;
        if (clearQueue)
        {
            s_outputQueue.clear();
            s_ioWriteQueue.clear();
            s_activeIoPulses.clear();
            s_pendingTaskInspection.reset();
        }
    }
    s_outputWorkerCondition.notify_all();
    if (s_outputWorker.joinable())
        s_outputWorker.join();
    std::lock_guard<std::mutex> lock(s_outputWorkerMutex);
    s_outputWorkerStop = false;
    s_outputWorkerBusy = false;
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
    config.orientation = std::clamp(config.orientation, 0, 5);
    config.reconnectFailureThreshold = (std::max)(1, config.reconnectFailureThreshold);
    config.reconnectInitialDelayMs = (std::max)(1, config.reconnectInitialDelayMs);
    config.reconnectMaxDelayMs = (std::max)(
        config.reconnectInitialDelayMs, config.reconnectMaxDelayMs);
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
    config.orientation = std::clamp(config.orientation, 0, 5);
    config.reconnectFailureThreshold = (std::max)(1, config.reconnectFailureThreshold);
    config.reconnectInitialDelayMs = (std::max)(1, config.reconnectInitialDelayMs);
    config.reconnectMaxDelayMs = (std::max)(
        config.reconnectInitialDelayMs, config.reconnectMaxDelayMs);
    DeviceOperationResult result = camera->StartStream();
    if (!result.success)
        return s_lastCameraOperation = std::move(result);

    const DeviceOperationResult autoExposure = ApplyCameraControls(camera, config);
    if (!autoExposure.success)
        s_lastCameraOperation = autoExposure;

    s_cameraConfig = std::move(config);
    s_cameraOrientation.store(s_cameraConfig.orientation);
    s_cameraFrameIndex = 0;
    s_cameraConsecutiveFailures = 0;
    s_cameraReconnectAttempts = 0;
    s_cameraReconnectDelayMs = 0;
    s_cameraReconnecting = false;
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

void SetCameraOrientation(int orientation)
{
    const int normalized = std::clamp(orientation, 0, 5);
    s_cameraOrientation.store(normalized);
    std::lock_guard<std::mutex> lock(s_cameraWorkerMutex);
    s_cameraConfig.orientation = normalized;
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
            s_cameraToolRunFrameAvailable = false;
        }
    }
    s_cameraWorkerCondition.notify_all();
}

void CancelPendingCameraToolRun()
{
    {
        std::lock_guard<std::mutex> lock(s_cameraWorkerMutex);
        const bool hadPendingToolRun = s_runToolChainAfterFrameIndex >= 0 ||
            s_cameraToolRunPending;
        s_runToolChainAfterFrameIndex = -1;
        s_cameraToolRunPending = false;
        s_cameraToolRunLoop = false;
        s_cameraToolRunFrameAvailable = false;
        if (hadPendingToolRun && !s_cameraAutoCapture)
            s_cameraFrameRequested = false;
    }
    s_cameraWorkerCondition.notify_all();
}

DeviceOperationResult ConnectOutput(const HardwareOutputConnectionConfig& rawConfig)
{
    StopOutputWorker(true);
    HardwareOutputConnectionConfig config = rawConfig;
    if (config.binding.adapterKey.empty())
        return s_lastOutputOperation = InvalidConfiguration("设备适配器标识为空");
    if (config.endpoint.address.empty())
        return s_lastOutputOperation = InvalidConfiguration("设备地址为空");
    config.maxQueueSize = (std::max)(1, config.maxQueueSize);
    config.retryCount = std::clamp(config.retryCount, 0, 10);
    config.retryDelayMs = (std::max)(1, config.retryDelayMs);
    config.handshake.pollIntervalMs = std::clamp(
        config.handshake.pollIntervalMs, 10, 5000);
    config.handshake.acknowledgementTimeoutMs = std::clamp(
        config.handshake.acknowledgementTimeoutMs, 100, 60000);
    config.handshake.inspectionTimeoutMs = std::clamp(
        config.handshake.inspectionTimeoutMs, 1000, 600000);
    config.handshake.heartbeatIntervalMs = std::clamp(
        config.handshake.heartbeatIntervalMs, 100, 60000);
    config.handshake.reconnectFailureThreshold = std::clamp(
        config.handshake.reconnectFailureThreshold, 1, 100);
    config.handshake.reconnectInitialDelayMs = std::clamp(
        config.handshake.reconnectInitialDelayMs, 1, 60000);
    config.handshake.reconnectMaxDelayMs = std::clamp(
        config.handshake.reconnectMaxDelayMs,
        config.handshake.reconnectInitialDelayMs, 60000);
    for (HardwareIoMapping& mapping : config.handshake.mappings)
        mapping.pulseMs = std::clamp(mapping.pulseMs, 0, 60000);
    if (config.handshake.enabled &&
        config.adapterType != HardwareOutputAdapterType::ModbusTcp)
    {
        return s_lastOutputOperation = InvalidConfiguration(
            "PLC IO 握手仅支持 Modbus TCP 输出类型");
    }
    const DeviceOperationResult handshakeValidation =
        ValidateHandshakeConfig(config.handshake);
    if (!handshakeValidation.success)
        return s_lastOutputOperation = handshakeValidation;

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

    std::lock_guard<std::mutex> adapterLock(s_outputAdapterMutex);
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
    s_outputAutoPublish = s_outputConfig.autoPublish &&
        !s_outputConfig.handshake.enabled;
    {
        std::lock_guard<std::mutex> workerLock(s_outputWorkerMutex);
        ResetHandshakeStateLocked(true);
        s_outputConsecutiveFailures = 0;
        s_outputReconnectAttempts = 0;
        s_outputReconnectDelayMs = 0;
        s_outputReconnecting = false;
        s_outputCommunicationAlarm = false;
        s_outputLastCommunicationTimestampMs = CurrentTimestampMs();
    }
    s_lastOutputOperation = {true, result.message.empty()
        ? "硬件输出已连接"
        : std::move(result.message)};
    StartOutputWorker();
    return s_lastOutputOperation;
}

void DisconnectOutput()
{
    StopOutputWorker(true);
    {
        std::lock_guard<std::mutex> adapterLock(s_outputAdapterMutex);
        if (!s_outputAdapterKey.empty())
            HardwareAdapterService::Remove(s_outputAdapterKey);
        s_outputAdapterKey.clear();
    }
    s_outputAutoPublish = false;
    {
        std::lock_guard<std::mutex> workerLock(s_outputWorkerMutex);
        ResetHandshakeStateLocked(true);
        s_lastOutputOperation = {true, "硬件输出已断开"};
    }
}

void ConfigureOutputBinding(const HardwareOutputBinding& binding, bool autoPublish)
{
    {
        std::scoped_lock lock(s_outputWorkerMutex, s_outputAdapterMutex);
        s_outputBinding = binding;
        s_outputAdapterKey = binding.adapterKey;
        s_outputAutoPublish = autoPublish;
    }
    StartOutputWorker();
}

void ConfigureModbusHandshake(const HardwareHandshakeConfig& rawConfig)
{
    HardwareHandshakeConfig config = rawConfig;
    config.pollIntervalMs = std::clamp(config.pollIntervalMs, 10, 5000);
    config.acknowledgementTimeoutMs = std::clamp(
        config.acknowledgementTimeoutMs, 100, 60000);
    config.inspectionTimeoutMs = std::clamp(
        config.inspectionTimeoutMs, 1000, 600000);
    config.heartbeatIntervalMs = std::clamp(
        config.heartbeatIntervalMs, 100, 60000);
    config.reconnectFailureThreshold = std::clamp(
        config.reconnectFailureThreshold, 1, 100);
    config.reconnectInitialDelayMs = std::clamp(
        config.reconnectInitialDelayMs, 1, 60000);
    config.reconnectMaxDelayMs = std::clamp(config.reconnectMaxDelayMs,
        config.reconnectInitialDelayMs, 60000);
    for (HardwareIoMapping& mapping : config.mappings)
        mapping.pulseMs = std::clamp(mapping.pulseMs, 0, 60000);

    {
        std::lock_guard<std::mutex> lock(s_outputWorkerMutex);
        s_outputConfig.handshake = std::move(config);
        s_inputStates.assign(s_outputConfig.handshake.mappings.size(), false);
        s_inputStateInitialized.assign(
            s_outputConfig.handshake.mappings.size(), false);
        s_nextIoPollAt = std::chrono::steady_clock::now();
        s_nextHeartbeatAt = s_nextIoPollAt;
        ResetHandshakeStateLocked(true);
    }
    StartOutputWorker();
    s_outputWorkerCondition.notify_one();
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
    std::lock_guard<std::mutex> adapterLock(s_outputAdapterMutex);
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

DeviceOperationResult TestIoMapping(std::size_t mappingIndex, bool active)
{
    HardwareIoMapping mapping;
    {
        std::lock_guard<std::mutex> lock(s_outputWorkerMutex);
        if (!s_outputConfig.handshake.enabled)
            return {false, "请先启用并连接 PLC IO 握手"};
        if (mappingIndex >= s_outputConfig.handshake.mappings.size())
            return {false, "IO 映射索引无效"};
        mapping = s_outputConfig.handshake.mappings[mappingIndex];
    }
    if (!mapping.enabled)
        return {false, "该 IO 映射未启用"};

    DeviceOperationResult result;
    if (mapping.direction == HardwareIoDirection::Output)
    {
        result = WriteIoMapping(mapping, active);
    }
    else
    {
        std::lock_guard<std::mutex> adapterLock(s_outputAdapterMutex);
        IDeviceAdapter* adapter = HardwareAdapterService::Find(s_outputAdapterKey);
        auto* modbus = dynamic_cast<IModbusTcpAdapter*>(adapter);
        if (!adapter || !modbus)
            result = {false, "当前适配器不支持 Modbus TCP IO 读取"};
        else if (adapter->ConnectionState() != DeviceConnectionState::Connected)
            result = NotConnected(adapter->AdapterName());
        else
        {
            std::vector<bool> values;
            result = modbus->ReadCoils(mapping.address, 1, values);
            if (result.success && values.size() == 1)
            {
                const bool logical = mapping.invert ? !values.front() : values.front();
                result.message = std::string("输入状态: ") + (logical ? "ON" : "OFF");
            }
            else if (result.success)
            {
                result = {false, "Modbus 输入线圈返回为空"};
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(s_outputWorkerMutex);
        RecordCommunicationResultLocked(result);
    }
    return result;
}

DeviceOperationResult RequestHandshakeTest(ToolResultStatus status)
{
    std::lock_guard<std::mutex> lock(s_outputWorkerMutex);
    if (!s_outputConfig.handshake.enabled)
        return {false, "请先启用并连接 PLC IO 握手"};
    if (s_handshakeActive || s_handshakeTestRequested ||
        s_pendingTaskInspection.has_value())
        return {false, "已有 PLC 握手周期正在进行"};
    s_handshakeTestStatus = status;
    s_handshakeTestRequested = true;
    s_outputWorkerCondition.notify_one();
    return {true, "整套握手测试请求已接收"};
}

DeviceOperationResult RequestTaskInspection(const std::string& taskGroupName,
    bool preferCamera)
{
    if (taskGroupName.empty())
        return {false, "触发任务名称为空"};
    const int taskIndex = ToolChainState::TaskGroupIndexByName(taskGroupName);
    if (taskIndex < 0)
        return {false, "任务不存在: " + taskGroupName};
    if (!ToolChainState::ReadOnlyTaskGroups()[taskIndex].enabled)
        return {false, "任务已禁用: " + taskGroupName};

    std::lock_guard<std::mutex> lock(s_outputWorkerMutex);
    if (!s_outputConfig.handshake.enabled)
        return {false, "请先启用并连接 PLC IO 握手"};
    if (s_handshakeActive || s_handshakeTestRequested ||
        s_pendingTaskInspection.has_value())
    {
        ++s_handshakeIgnoredTriggerCount;
        return {false, "PLC 握手忙碌，本次触发已忽略（不会排队补跑）"};
    }
    s_pendingTaskInspection = PendingTaskInspection{taskGroupName, preferCamera};
    return {true, "任务触发已接收: " + taskGroupName};
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

DeviceOperationResult EnqueueConfiguredStatus(ToolResultStatus status)
{
    StartOutputWorker();
    std::lock_guard<std::mutex> lock(s_outputWorkerMutex);
    const std::size_t maximumQueue = static_cast<std::size_t>((std::max)(1,
        s_outputConfig.maxQueueSize));
    if (s_outputQueue.size() >= maximumQueue)
    {
        s_outputQueue.pop_front();
        ++s_outputDroppedCount;
    }
    PendingOutput pending;
    pending.status = status;
    pending.binding = s_outputBinding;
    pending.sequence = ++s_outputSequence;
    s_outputQueue.push_back(std::move(pending));
    s_outputWorkerCondition.notify_one();
    return {true, "硬件输出已加入发送队列"};
}

DeviceOperationResult PublishConfiguredStatus(ToolResultStatus status)
{
    DeviceOperationResult result = PublishInspectionStatus(status, s_outputBinding);
    std::lock_guard<std::mutex> lock(s_outputWorkerMutex);
    s_lastOutputOperation = result;
    return result;
}

DeviceOperationResult PublishInspectionResults(const std::vector<ToolResult>& results)
{
    return PublishConfiguredStatus(AggregateInspectionStatus(results));
}

bool WaitForOutputIdle(int timeoutMs)
{
    std::unique_lock<std::mutex> lock(s_outputWorkerMutex);
    return s_outputWorkerCondition.wait_for(lock,
        std::chrono::milliseconds((std::max)(1, timeoutMs)), []
        {
            return s_outputQueue.empty() && s_ioWriteQueue.empty() &&
                s_activeIoPulses.empty() && !s_outputWorkerBusy;
        });
}

void Tick()
{
    PendingCameraFrame pending;
    bool hasPendingFrame = false;
    bool completeToolRunRequest = false;
    bool cameraFrameAvailable = false;
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
                completeToolRunRequest = true;
                cameraFrameAvailable = pending.operation.success &&
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
        }
        if (completeToolRunRequest)
        {
            std::lock_guard<std::mutex> lock(s_cameraWorkerMutex);
            s_cameraToolRunPending = true;
            s_cameraToolRunLoop = runToolChainLoop;
            s_cameraToolRunFrameAvailable = cameraFrameAvailable;
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
        bool frameAvailable = false;
        {
            std::lock_guard<std::mutex> lock(s_cameraWorkerMutex);
            loop = s_cameraToolRunLoop;
            frameAvailable = s_cameraToolRunFrameAvailable;
            s_cameraToolRunLoop = false;
            s_cameraToolRunFrameAvailable = false;
        }
        ToolController::ResumeRunAfterCamera(loop, frameAvailable);
        s_lastCameraOperation.message = frameAvailable
            ? "工业相机帧已发布，工具链已开始执行"
            : "工业相机抓帧失败，工具链使用任务备用图片继续执行";
    }

    PendingTaskInspection inspectionRequest;
    bool dispatchInspection = false;
    bool cancelTimedOutInspection = false;
    {
        std::lock_guard<std::mutex> lock(s_outputWorkerMutex);
        const auto now = std::chrono::steady_clock::now();

        if (s_handshakeAcknowledgeReceived)
        {
            s_handshakeAcknowledgeReceived = false;
            if (s_handshakeAwaitingAcknowledge)
            {
                QueueHandshakeResetLocked();
                s_handshakeAwaitingAcknowledge = false;
                s_handshakeActive = false;
                s_handshakeTestActive = false;
                s_handshakeTaskGroupName.clear();
                s_handshakeAlarm = false;
                s_handshakeAlarmMessage.clear();
                s_lastOutputOperation = {true, "PLC 已确认本轮检测结果"};
            }
        }

        if (s_handshakeTestRequested && !s_handshakeActive)
        {
            s_handshakeTestRequested = false;
            s_handshakeTestActive = true;
            s_handshakeActive = true;
            s_handshakeAwaitingAcknowledge = false;
            s_handshakeTaskGroupName = "握手自检";
            s_handshakeStartedAt = now;
            s_handshakeTestCompleteAt = now + std::chrono::milliseconds(300);
            QueueHandshakeStartLocked();
        }

        if (s_handshakeTestActive && !s_handshakeAwaitingAcknowledge &&
            now >= s_handshakeTestCompleteAt)
        {
            CompleteHandshakeLocked(s_handshakeTestStatus);
        }

        if (s_handshakeActive && !s_handshakeTestActive &&
            !s_handshakeAwaitingAcknowledge)
        {
            const bool completed =
                ToolController::GetCompletedBatchSerial() >
                    s_handshakeStartBatchSerial &&
                ToolController::GetMode() == ToolController::Mode::Idle &&
                ToolController::WasLastRunTaskGroup() &&
                ToolController::GetLastRunTaskGroupName() ==
                    s_handshakeTaskGroupName;
            if (completed)
            {
                CompleteHandshakeLocked(
                    AggregateTaskGroupStatus(s_handshakeTaskGroupName));
            }
            else if (now - s_handshakeStartedAt >= std::chrono::milliseconds(
                s_outputConfig.handshake.inspectionTimeoutMs))
            {
                cancelTimedOutInspection = true;
                s_handshakeAlarm = true;
                s_handshakeAlarmMessage =
                    "单任务拍照检测超时: " + s_handshakeTaskGroupName;
                s_lastOutputOperation = {false, s_handshakeAlarmMessage};
                CompleteHandshakeLocked(ToolResultStatus::Error);
            }
        }

        if (s_handshakeAwaitingAcknowledge &&
            now - s_handshakeCompletedAt >= std::chrono::milliseconds(
                s_outputConfig.handshake.acknowledgementTimeoutMs))
        {
            s_handshakeAlarm = true;
            s_handshakeAlarmMessage = "PLC 结果确认应答超时";
            s_lastOutputOperation = {false, s_handshakeAlarmMessage};
            QueueHandshakeResetLocked();
            s_handshakeAwaitingAcknowledge = false;
            s_handshakeActive = false;
            s_handshakeTestActive = false;
            s_handshakeTaskGroupName.clear();
        }

        if (!s_handshakeActive && !s_handshakeTestRequested &&
            ToolController::GetMode() == ToolController::Mode::Idle &&
            s_pendingTaskInspection.has_value())
        {
            inspectionRequest = std::move(*s_pendingTaskInspection);
            s_pendingTaskInspection.reset();
            s_handshakeActive = true;
            s_handshakeAwaitingAcknowledge = false;
            s_handshakeTestActive = false;
            s_handshakeTaskGroupName = inspectionRequest.taskGroupName;
            s_handshakeStartBatchSerial =
                ToolController::GetCompletedBatchSerial();
            s_handshakeStartedAt = now;
            QueueHandshakeStartLocked();
            dispatchInspection = true;
        }
    }

    if (cancelTimedOutInspection)
        ToolController::Reset();

    if (dispatchInspection)
    {
        const int taskIndex = ToolChainState::TaskGroupIndexByName(
            inspectionRequest.taskGroupName);
        const bool taskAvailable = taskIndex >= 0 &&
            ToolChainState::ReadOnlyTaskGroups()[taskIndex].enabled;
        if (!taskAvailable)
        {
            std::lock_guard<std::mutex> lock(s_outputWorkerMutex);
            s_lastOutputOperation = {false,
                "PLC 触发的任务不存在或已禁用: " +
                    inspectionRequest.taskGroupName};
            s_handshakeAlarm = true;
            s_handshakeAlarmMessage = s_lastOutputOperation.message;
            CompleteHandshakeLocked(ToolResultStatus::Error);
        }
        else
        {
            const bool useCamera = inspectionRequest.preferCamera &&
                Snapshot().cameraState == DeviceConnectionState::Connected;
            ToolController::RequestRunTaskGroup(
                inspectionRequest.taskGroupName, false,
                useCamera, useCamera);
        }
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
    {
        std::lock_guard<std::mutex> adapterLock(s_outputAdapterMutex);
        if (!s_outputAdapterKey.empty())
        {
            if (const IDeviceAdapter* output =
                HardwareAdapterService::FindReadOnly(s_outputAdapterKey))
            {
                snapshot.outputState = output->ConnectionState();
                snapshot.outputAdapterName = output->AdapterName();
            }
        }
        snapshot.outputAdapterKey = s_outputAdapterKey;
    }
    {
        std::lock_guard<std::mutex> lock(s_cameraWorkerMutex);
        snapshot.cameraAutoCapture = s_cameraAutoCapture;
        snapshot.cameraTriggerOnInspection = s_cameraTriggerOnInspection;
        snapshot.cameraCapturePending = s_cameraWorkerBusy || s_hasPendingCameraFrame;
        snapshot.cameraToolRunPending = s_runToolChainAfterFrameIndex >= 0 ||
            s_cameraToolRunPending;
        snapshot.cameraReconnecting = s_cameraReconnecting;
        snapshot.cameraConsecutiveFailures = s_cameraConsecutiveFailures;
        snapshot.cameraReconnectAttempts = s_cameraReconnectAttempts;
        snapshot.cameraReconnectDelayMs = s_cameraReconnectDelayMs;
        snapshot.lastCameraOperation = s_lastCameraOperation;
    }
    {
        std::lock_guard<std::mutex> lock(s_outputWorkerMutex);
        snapshot.outputQueueBusy = s_outputWorkerBusy;
        snapshot.outputQueueDepth = s_outputQueue.size();
        snapshot.outputSentCount = s_outputSentCount;
        snapshot.outputFailedCount = s_outputFailedCount;
        snapshot.outputDroppedCount = s_outputDroppedCount;
        snapshot.lastOutputOperation = s_lastOutputOperation;
        snapshot.outputConsecutiveFailures = s_outputConsecutiveFailures;
        snapshot.outputReconnectAttempts = s_outputReconnectAttempts;
        snapshot.outputReconnectDelayMs = s_outputReconnectDelayMs;
        snapshot.outputReconnecting = s_outputReconnecting;
        snapshot.outputCommunicationAlarm = s_outputCommunicationAlarm;
        snapshot.outputLastCommunicationTimestampMs =
            s_outputLastCommunicationTimestampMs;
        snapshot.handshakeEnabled = s_outputConfig.handshake.enabled;
        snapshot.handshakeActive = s_handshakeActive;
        snapshot.handshakeAwaitingAcknowledge =
            s_handshakeAwaitingAcknowledge;
        snapshot.handshakeTestActive = s_handshakeTestActive;
        snapshot.handshakeIgnoredTriggerCount =
            s_handshakeIgnoredTriggerCount;
        snapshot.handshakeTaskGroupName = s_handshakeTaskGroupName;
        snapshot.handshakeAlarm = s_handshakeAlarm;
        snapshot.handshakeAlarmMessage = s_handshakeAlarmMessage;
    }
    snapshot.outputAutoPublish = s_outputAutoPublish;
    snapshot.cameraFrameIndex = s_cameraFrameIndex;
    return snapshot;
}

void Shutdown()
{
    StopCameraWorker();
    StopOutputWorker(true);
    FrameArchiveService::Shutdown();
    HardwareAdapterService::Clear();
    s_outputAdapterKey.clear();
    s_outputAutoPublish = false;
}
}
