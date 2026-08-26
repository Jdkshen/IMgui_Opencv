#include "HardwareWindow.h"

#include "DockSpaceHost.h"
#include "../Core/FrameArchiveService.h"
#include "../Core/HardwareRuntimeService.h"
#include "../Core/HardwareSettingsService.h"
#include "../Core/ToolChainState.h"
#include "../Core/OpenFileDialog.h"
#include "../Core/ThemeManager.h"
#include "../Log/LogSystem.h"
#include "../include/imgui/imgui.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <cstring>
#include <string>
#include <vector>

namespace
{
const char* ConnectionStateName(DeviceConnectionState state)
{
    switch (state)
    {
    case DeviceConnectionState::Disconnected: return "未连接";
    case DeviceConnectionState::Connecting: return "连接中";
    case DeviceConnectionState::Connected: return "已连接";
    case DeviceConnectionState::Fault: return "故障";
    default: return "未知";
    }
}

ImVec4 ConnectionStateColor(DeviceConnectionState state)
{
    switch (state)
    {
    case DeviceConnectionState::Connected: return ImVec4(0.25f, 0.80f, 0.42f, 1.0f);
    case DeviceConnectionState::Connecting: return ImVec4(0.95f, 0.72f, 0.22f, 1.0f);
    case DeviceConnectionState::Fault: return ImVec4(0.95f, 0.30f, 0.28f, 1.0f);
    default: return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    }
}

void LogOperation(const char* action, const DeviceOperationResult& result)
{
    LogSystem::Add(result.success ? LOG_INFO : LOG_ERROR, "%s%s: %s",
        action, result.success ? "成功" : "失败", result.message.c_str());
}

void DrawOperationMessage(const DeviceOperationResult& result)
{
    if (result.message.empty())
        return;
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(result.success
        ? ImVec4(0.35f, 0.78f, 0.48f, 1.0f)
        : ImVec4(0.95f, 0.38f, 0.32f, 1.0f),
        "%s", result.message.c_str());
    ImGui::PopTextWrapPos();
}

void DrawSectionTitle(const char* label)
{
    const bool isDark = g_CurrentTheme == 0;
    ImGui::PushStyleColor(ImGuiCol_Text, isDark
        ? ImVec4(0.42f, 0.78f, 0.84f, 1.0f)
        : ImVec4(0.05f, 0.39f, 0.46f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Separator, isDark
        ? ImVec4(0.18f, 0.36f, 0.40f, 1.0f)
        : ImVec4(0.48f, 0.67f, 0.70f, 1.0f));
    ImGui::SeparatorText(label);
    ImGui::PopStyleColor(2);
}

std::uint16_t ClampPort(int value)
{
    return static_cast<std::uint16_t>(std::clamp(value, 0, 65535));
}

std::uint16_t ClampAddress(int value)
{
    return static_cast<std::uint16_t>(std::clamp(value, 0, 65535));
}

bool BeginPropertyTable(const char* id)
{
    if (!ImGui::BeginTable(id, 2,
        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX))
    {
        return false;
    }
    const float available = ImGui::GetContentRegionAvail().x;
    const float labelWidth = available < 260.0f
        ? std::clamp(available * 0.40f, 68.0f, 82.0f)
        : 104.0f;
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

void PropertyRow(const char* label)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", label);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-1.0f);
}

float TwoColumnButtonWidth()
{
    return (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
}

constexpr float kActionButtonHeight = 28.0f;

struct CameraUiState
{
    bool connectOnStartup = false;
    char address[256] = "0";
    char sourceName[96] = "camera-01";
    int backend = 0;
    int orientation = 0;
    int timeoutMs = 250;
    int intervalMs = 33;
    bool autoCapture = true;
    bool runAfterCapture = true;
    bool triggerBeforeRun = true;
    bool autoExposure = true;
    float exposure = -6.0f;
    float gain = 0.0f;
    int triggerMode = 0;
    float triggerDelayMicroseconds = 0.0f;
    int bufferPolicy = 0;
    bool ptpEnabled = false;
    bool autoReconnect = true;
    int reconnectFailureThreshold = 3;
    int reconnectInitialDelayMs = 250;
    int reconnectMaxDelayMs = 5000;
};

struct AuxiliaryOutputUiState
{
    bool enabled = false;
    int type = static_cast<int>(HardwareOutputAdapterType::TcpText);
    char key[96] = {};
    char address[256] = "127.0.0.1";
    int port = 5001;
    char resource[128] = {};
    char target[256] = {};
    int mappingAddress = 0;
    int timeoutMs = 1500;
    bool plcHoldingRegister = false;
    char passText[128] = "PASS";
    char failText[128] = "FAIL";
    bool sendQrJson = false;
    bool appendCrLf = true;
    bool invert = false;
    bool autoPublish = true;
    int queueSize = 32;
    int retryCount = 2;
    int retryDelayMs = 150;
    bool reconnectBeforeRetry = true;
};

void LoadAuxiliaryOutputUi(AuxiliaryOutputUiState& target,
    const HardwareOutputConnectionConfig& source)
{
    target.enabled = source.enabled;
    target.type = static_cast<int>(source.adapterType);
    std::snprintf(target.key, sizeof(target.key), "%s",
        source.binding.adapterKey.c_str());
    std::snprintf(target.address, sizeof(target.address), "%s",
        source.endpoint.address.c_str());
    target.port = source.endpoint.port;
    std::snprintf(target.resource, sizeof(target.resource), "%s",
        source.endpoint.resource.c_str());
    std::snprintf(target.target, sizeof(target.target), "%s",
        source.binding.target.c_str());
    target.mappingAddress = source.binding.address;
    target.timeoutMs = source.endpoint.timeoutMs;
    target.plcHoldingRegister = source.plcUseHoldingRegister;
    std::snprintf(target.passText, sizeof(target.passText), "%s",
        source.binding.passText.c_str());
    std::snprintf(target.failText, sizeof(target.failText), "%s",
        source.binding.failText.c_str());
    target.sendQrJson = source.binding.sendQrJson;
    target.appendCrLf = source.binding.appendCrLf;
    target.invert = source.binding.invert;
    target.autoPublish = source.autoPublish;
    target.queueSize = source.maxQueueSize;
    target.retryCount = source.retryCount;
    target.retryDelayMs = source.retryDelayMs;
    target.reconnectBeforeRetry = source.reconnectBeforeRetry;
}

HardwareOutputConnectionConfig BuildAuxiliaryOutputConfig(
    const AuxiliaryOutputUiState& source)
{
    HardwareOutputConnectionConfig config;
    config.enabled = source.enabled;
    config.adapterType = static_cast<HardwareOutputAdapterType>(
        std::clamp(source.type, 0, 3));
    config.endpoint.address = source.address;
    config.endpoint.port = ClampPort(source.port);
    config.endpoint.resource = source.resource;
    config.endpoint.timeoutMs = (std::max)(1, source.timeoutMs);
    config.binding.adapterKey = source.key;
    config.binding.target = source.target;
    config.binding.address = ClampAddress(source.mappingAddress);
    config.binding.passText = source.passText;
    config.binding.failText = source.failText;
    config.binding.sendQrJson = source.sendQrJson;
    config.binding.appendCrLf = source.appendCrLf;
    config.binding.invert = source.invert;
    config.plcUseHoldingRegister = source.plcHoldingRegister;
    config.autoPublish = source.autoPublish;
    config.maxQueueSize = (std::max)(1, source.queueSize);
    config.retryCount = std::clamp(source.retryCount, 0, 10);
    config.retryDelayMs = (std::max)(1, source.retryDelayMs);
    config.reconnectBeforeRetry = source.reconnectBeforeRetry;
    config.handshake.enabled = false;
    return config;
}

bool IsNarrowPanel()
{
    return ImGui::GetContentRegionAvail().x < 260.0f;
}

const char* IoSignalName(HardwareIoSignal signal)
{
    switch (signal)
    {
    case HardwareIoSignal::Trigger: return "触发 Trigger";
    case HardwareIoSignal::Busy: return "运行 Busy";
    case HardwareIoSignal::Done: return "完成 Done";
    case HardwareIoSignal::Ok: return "合格 OK";
    case HardwareIoSignal::Ng: return "不合格 NG";
    case HardwareIoSignal::Error: return "异常 Error";
    case HardwareIoSignal::Heartbeat: return "心跳 Heartbeat";
    case HardwareIoSignal::Acknowledge: return "确认 ACK";
    default: return "未知";
    }
}

bool DrawTaskSlotCombo(const char* id, std::string& selectedTask)
{
    if (!ImGui::BeginCombo(id, selectedTask.empty()
        ? "选择任务" : selectedTask.c_str()))
    {
        return false;
    }

    bool changed = false;
    const auto& taskGroups = ToolChainState::ReadOnlyTaskGroups();
    for (int taskSlot = 0;
        taskSlot < static_cast<int>(kHardwareCameraCount); ++taskSlot)
    {
        char slotName[32];
        std::snprintf(slotName, sizeof(slotName), "任务%02d", taskSlot + 1);
        if (taskSlot < static_cast<int>(taskGroups.size()))
        {
            const TaskGroupDefinition& group = taskGroups[taskSlot];
            const std::string display = group.name == slotName
                ? std::string(slotName)
                : std::string(slotName) + " · " + group.name;
            if (ImGui::Selectable(display.c_str(), selectedTask == group.name))
            {
                selectedTask = group.name;
                changed = true;
            }
        }
        else
        {
            ImGui::BeginDisabled();
            ImGui::Selectable((std::string(slotName) + "（未创建）").c_str());
            ImGui::EndDisabled();
        }
    }
    ImGui::EndCombo();
    return changed;
}

}

namespace UI
{
void DrawHardwarePanel(int page)
{
    static std::array<CameraUiState, kHardwareCameraCount> cameraStates;
    static int selectedCameraIndex = 0;
    static int cameraMaxConcurrentGrabs = 4;
    static int connectedCameraIndex = -1;
    static CameraDiscoveryResult cameraDiscovery;
    static int selectedDiscoveredCamera = -1;
    static char forceIpAddress[32] = "192.168.1.64";
    static char forceIpSubnet[32] = "255.255.255.0";
    static char forceIpGateway[32] = "0.0.0.0";
    static DeviceOperationResult forceIpOperation;
    static bool hardwareUiInitialized = false;
    static std::string hardwareSettingsError;
    static bool archiveUiInitialized = false;
    static FrameArchiveConfig archiveConfig;
    static char archiveDirectory[512] = {};

    static int outputType = 0;
    static char outputKey[96] = "output-main";
    static char outputAddress[256] = "127.0.0.1";
    static int outputPort = 502;
    static char outputResource[128] = "1";
    static char outputTarget[256] = "ns=2;s=Inspection.OK";
    static int outputAddressValue = 0;
    static int outputTimeoutMs = 1500;
    static bool plcHoldingRegister = false;
    static char tcpPassText[128] = "PASS";
    static char tcpFailText[128] = "FAIL";
    static bool tcpSendQrJson = false;
    static bool tcpAppendCrLf = true;
    static bool outputInvert = false;
    static bool outputAutoPublish = false;
    static int outputQueueSize = 32;
    static int outputRetryCount = 2;
    static int outputRetryDelayMs = 150;
    static bool outputReconnectBeforeRetry = true;
    static bool outputHandshakeEnabled = false;
    static int outputPollIntervalMs = 50;
    static int outputAcknowledgementTimeoutMs = 3000;
    static int outputInspectionTimeoutMs = 30000;
    static int outputHeartbeatIntervalMs = 1000;
    static bool outputAutoReconnect = true;
    static int outputReconnectFailureThreshold = 3;
    static int outputReconnectInitialDelayMs = 250;
    static int outputReconnectMaxDelayMs = 5000;
    static std::vector<HardwareIoMapping> outputIoMappings;
    static std::array<AuxiliaryOutputUiState,
        kHardwareAuxiliaryOutputCount> auxiliaryOutputStates;
    static std::array<DeviceOperationResult,
        kHardwareAuxiliaryOutputCount> auxiliaryOutputOperations;
    static int selectedAuxiliaryOutput = 0;
    static std::string manualTriggerTask;
    static std::string triggerSyncMessage;
    static std::vector<HardwareTaskIdentity> synchronizedTaskGroups;
    static bool outputConfigurationDirty = false;

    if (!hardwareUiInitialized)
    {
        const HardwarePanelSettings settings = HardwareSettingsService::Load();
        selectedCameraIndex = std::clamp(settings.activeCameraIndex, 0,
            static_cast<int>(kHardwareCameraCount) - 1);
        cameraMaxConcurrentGrabs = settings.cameraMaxConcurrentGrabs;
        HardwareRuntimeService::SetCameraMaxConcurrentGrabs(
            cameraMaxConcurrentGrabs);
        for (std::size_t index = 0; index < cameraStates.size(); ++index)
        {
            const HardwareCameraSettings& source = settings.cameras[index];
            CameraUiState& target = cameraStates[index];
            target.connectOnStartup = source.connectOnStartup;
            std::snprintf(target.address, sizeof(target.address), "%s", source.address.c_str());
            std::snprintf(target.sourceName, sizeof(target.sourceName), "%s",
                source.sourceName.c_str());
            target.backend = source.backend;
            target.orientation = source.orientation;
            target.timeoutMs = source.timeoutMs;
            target.intervalMs = source.intervalMs;
            target.autoCapture = source.autoCapture;
            target.runAfterCapture = source.runAfterCapture;
            target.triggerBeforeRun = source.triggerBeforeRun;
            target.autoExposure = source.autoExposure;
            target.exposure = source.exposure;
            target.gain = source.gain;
            target.triggerMode = source.triggerMode;
            target.triggerDelayMicroseconds = source.triggerDelayMicroseconds;
            target.bufferPolicy = source.bufferPolicy;
            target.ptpEnabled = source.ptpEnabled;
            target.autoReconnect = source.autoReconnect;
            target.reconnectFailureThreshold = source.reconnectFailureThreshold;
            target.reconnectInitialDelayMs = source.reconnectInitialDelayMs;
            target.reconnectMaxDelayMs = source.reconnectMaxDelayMs;
        }

        outputType = settings.outputType;
        std::snprintf(outputKey, sizeof(outputKey), "%s", settings.outputKey.c_str());
        std::snprintf(outputAddress, sizeof(outputAddress), "%s", settings.outputAddress.c_str());
        outputPort = settings.outputPort;
        std::snprintf(outputResource, sizeof(outputResource), "%s", settings.outputResource.c_str());
        std::snprintf(outputTarget, sizeof(outputTarget), "%s", settings.outputTarget.c_str());
        outputAddressValue = settings.outputAddressValue;
        outputTimeoutMs = settings.outputTimeoutMs;
        plcHoldingRegister = settings.plcHoldingRegister;
        std::snprintf(tcpPassText, sizeof(tcpPassText), "%s", settings.tcpPassText.c_str());
        std::snprintf(tcpFailText, sizeof(tcpFailText), "%s", settings.tcpFailText.c_str());
        tcpSendQrJson = settings.tcpSendQrJson;
        tcpAppendCrLf = settings.tcpAppendCrLf;
        outputInvert = settings.outputInvert;
        outputAutoPublish = settings.outputAutoPublish;
        outputQueueSize = settings.outputQueueSize;
        outputRetryCount = settings.outputRetryCount;
        outputRetryDelayMs = settings.outputRetryDelayMs;
        outputReconnectBeforeRetry = settings.outputReconnectBeforeRetry;
        outputHandshakeEnabled = settings.outputHandshakeEnabled;
        outputPollIntervalMs = settings.outputPollIntervalMs;
        outputAcknowledgementTimeoutMs = settings.outputAcknowledgementTimeoutMs;
        outputInspectionTimeoutMs = settings.outputInspectionTimeoutMs;
        outputHeartbeatIntervalMs = settings.outputHeartbeatIntervalMs;
        outputAutoReconnect = settings.outputAutoReconnect;
        outputReconnectFailureThreshold = settings.outputReconnectFailureThreshold;
        outputReconnectInitialDelayMs = settings.outputReconnectInitialDelayMs;
        outputReconnectMaxDelayMs = settings.outputReconnectMaxDelayMs;
        outputIoMappings = settings.outputIoMappings;
        for (std::size_t index = 0; index < auxiliaryOutputStates.size(); ++index)
            LoadAuxiliaryOutputUi(auxiliaryOutputStates[index],
                settings.auxiliaryOutputs[index]);
        hardwareUiInitialized = true;
    }

    if (!archiveUiInitialized)
    {
        archiveConfig = FrameArchiveService::Config();
        std::snprintf(archiveDirectory, sizeof(archiveDirectory), "%s",
            archiveConfig.directory.c_str());
        archiveUiInitialized = true;
    }

    HardwareRuntimeSnapshot snapshot = HardwareRuntimeService::Snapshot();
    connectedCameraIndex = -1;
    auto cameraSlotSnapshot = [&snapshot](int index)
        -> const HardwareCameraSlotSnapshot*
    {
        const auto found = std::find_if(snapshot.cameraSlots.begin(),
            snapshot.cameraSlots.end(),
            [index](const HardwareCameraSlotSnapshot& item)
            {
                return item.slotIndex == index;
            });
        return found == snapshot.cameraSlots.end() ? nullptr : &*found;
    };
    if (const HardwareCameraSlotSnapshot* selectedSlot =
        cameraSlotSnapshot(snapshot.cameraSlotIndex))
    {
        if (selectedSlot->state == DeviceConnectionState::Connected)
            connectedCameraIndex = snapshot.cameraSlotIndex;
    }
    bool hardwareSettingsChanged = false;
    CameraUiState& selectedCamera = cameraStates[static_cast<std::size_t>(selectedCameraIndex)];
    bool& cameraConnectOnStartup = selectedCamera.connectOnStartup;
    auto& cameraAddress = selectedCamera.address;
    auto& cameraSourceName = selectedCamera.sourceName;
    int& cameraBackend = selectedCamera.backend;
    int& cameraOrientation = selectedCamera.orientation;
    int& cameraTimeoutMs = selectedCamera.timeoutMs;
    int& cameraIntervalMs = selectedCamera.intervalMs;
    bool& cameraAutoCapture = selectedCamera.autoCapture;
    bool& cameraRunAfterCapture = selectedCamera.runAfterCapture;
    bool& cameraTriggerBeforeRun = selectedCamera.triggerBeforeRun;
    bool& cameraAutoExposure = selectedCamera.autoExposure;
    float& cameraExposure = selectedCamera.exposure;
    float& cameraGain = selectedCamera.gain;
    int& cameraTriggerMode = selectedCamera.triggerMode;
    float& cameraTriggerDelay = selectedCamera.triggerDelayMicroseconds;
    int& cameraBufferPolicy = selectedCamera.bufferPolicy;
    bool& cameraPtpEnabled = selectedCamera.ptpEnabled;
    bool& cameraAutoReconnect = selectedCamera.autoReconnect;
    int& cameraReconnectFailureThreshold = selectedCamera.reconnectFailureThreshold;
    int& cameraReconnectInitialDelayMs = selectedCamera.reconnectInitialDelayMs;
    int& cameraReconnectMaxDelayMs = selectedCamera.reconnectMaxDelayMs;

    std::vector<HardwareTaskIdentity> currentTaskGroups;
    std::vector<std::string> currentTaskGroupNames;
    currentTaskGroups.reserve(ToolChainState::ReadOnlyTaskGroups().size());
    currentTaskGroupNames.reserve(ToolChainState::ReadOnlyTaskGroups().size());
    for (const TaskGroupDefinition& group : ToolChainState::ReadOnlyTaskGroups())
    {
        currentTaskGroups.push_back({group.id, group.name});
        currentTaskGroupNames.push_back(group.name);
    }
    if (currentTaskGroups != synchronizedTaskGroups)
    {
        if (HardwareSettingsService::SynchronizeTaskTriggerMappings(
            outputIoMappings, synchronizedTaskGroups, currentTaskGroups))
        {
            hardwareSettingsChanged = true;
            outputConfigurationDirty = true;
            triggerSyncMessage =
                "已同步任务 Trigger：保留地址并处理新增、重命名或删除";
        }
        synchronizedTaskGroups = std::move(currentTaskGroups);
    }

    const bool showCamera = page == 0;
    const bool showArchive = page == 1;
    const bool showOutput = page == 2;
    const bool showPlc = page == 3;
    const bool narrowPanel = IsNarrowPanel();
    const float twoButtonWidth = TwoColumnButtonWidth();
    const bool outputConnected = snapshot.outputState == DeviceConnectionState::Connected;

    if (showCamera)
    {
    DrawSectionTitle("16 路工业相机");
    ImGui::TextDisabled("资源调度：抓帧 %d/%d · 等待 %d · 单帧缓存 %.1f MiB",
        snapshot.cameraActiveGrabs, snapshot.cameraMaxConcurrentGrabs,
        snapshot.cameraWaitingGrabs,
        static_cast<double>(snapshot.cameraRetainedFrameBytes) /
            (1024.0 * 1024.0));
    const ImVec4 selectedSlotColor = g_CurrentTheme == 0
        ? ImVec4(0.08f, 0.40f, 0.46f, 1.0f)
        : ImVec4(0.04f, 0.48f, 0.56f, 1.0f);
    if (ImGui::BeginTable("##camera_slots", 8,
        ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX))
    {
        for (int index = 0; index < static_cast<int>(kHardwareCameraCount); ++index)
        {
            ImGui::TableNextColumn();
            ImGui::PushID(index);
            const bool selected = selectedCameraIndex == index;
            const HardwareCameraSlotSnapshot* slotSnapshot =
                cameraSlotSnapshot(index);
            const bool slotOnline = slotSnapshot &&
                slotSnapshot->state == DeviceConnectionState::Connected;
            if (selected)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, selectedSlotColor);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selectedSlotColor);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, selectedSlotColor);
            }
            char label[32];
            std::snprintf(label, sizeof(label), "相机 %02d%s", index + 1,
                slotOnline ? " · 在线" : "");
            if (ImGui::Button(label, ImVec2(-1.0f, 30.0f)))
            {
                selectedCameraIndex = index;
                if (slotOnline)
                    HardwareRuntimeService::ActivateCameraSlot(index, true);
                hardwareSettingsChanged = true;
            }
            if (selected)
                ImGui::PopStyleColor(3);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    const HardwareCameraSlotSnapshot* selectedSlotSnapshot =
        cameraSlotSnapshot(selectedCameraIndex);
    const bool cameraConnected = selectedSlotSnapshot &&
        selectedSlotSnapshot->state == DeviceConnectionState::Connected;
    ImGui::Text("当前配置：相机 %02d", selectedCameraIndex + 1);
    ImGui::SameLine();
    ImGui::TextColored(ConnectionStateColor(cameraConnected
        ? DeviceConnectionState::Connected : DeviceConnectionState::Disconnected), "%s%s%s",
        cameraConnected ? "已连接" : "未连接",
        !selectedSlotSnapshot || selectedSlotSnapshot->adapterName.empty() ? "" : " · ",
        selectedSlotSnapshot ? selectedSlotSnapshot->adapterName.c_str() : "");
    if (connectedCameraIndex >= 0 && connectedCameraIndex != selectedCameraIndex)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("（当前预览：相机 %02d）", connectedCameraIndex + 1);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("帧 %d%s",
        selectedSlotSnapshot ? selectedSlotSnapshot->frameIndex : 0,
        selectedSlotSnapshot && selectedSlotSnapshot->capturePending ? " · 抓取中" : "");
    if (selectedSlotSnapshot && (selectedSlotSnapshot->reconnecting ||
        selectedSlotSnapshot->consecutiveFailures > 0))
    {
        ImGui::TextDisabled("重连 %d 次 · 连续失败 %d · 退避 %d ms%s",
            selectedSlotSnapshot->reconnectAttempts,
            selectedSlotSnapshot->consecutiveFailures,
            selectedSlotSnapshot->reconnectDelayMs,
            selectedSlotSnapshot->reconnecting ? " · 重连中" : "");
    }

    const char* cameraBackends[] = {"自动", "DirectShow", "Media Foundation", "FFmpeg", "GStreamer", "海康机器人 MVS", "华睿 iRAYPLE"};
    if (cameraConnected)
    {
        ImGui::TextDisabled(
            "采集统计：接收 %llu · 丢帧 %llu · 不完整 %llu · 队列 %u",
            static_cast<unsigned long long>(selectedSlotSnapshot->statistics.receivedFrames),
            static_cast<unsigned long long>(selectedSlotSnapshot->statistics.droppedFrames),
            static_cast<unsigned long long>(selectedSlotSnapshot->statistics.incompleteFrames),
            selectedSlotSnapshot->statistics.queuedFrames);
        if (selectedSlotSnapshot->capabilities.hardwareTimestamp)
            ImGui::TextDisabled("硬件时间戳：支持");
        const CameraFrameMetadata& frameMetadata = selectedSlotSnapshot->frameMetadata;
        if (!frameMetadata.sourcePixelFormatName.empty())
        {
            ImGui::TextDisabled("源像素格式：%s · %d bit%s · 0x%08X",
                frameMetadata.sourcePixelFormatName.c_str(),
                frameMetadata.sourceBitDepth,
                frameMetadata.sourceIsBayer ? " · Bayer" : "",
                frameMetadata.sourcePixelFormat);
            ImGui::TextDisabled("显示转换：%s%s",
                frameMetadata.conversionPath.empty()
                    ? "未知" : frameMetadata.conversionPath.c_str(),
                frameMetadata.convertedToDisplay ? " · 已转换" : "");
        }
    }
    if (BeginPropertyTable("##camera_properties"))
    {
        PropertyRow("并发抓帧上限");
        if (ImGui::DragInt("##camera_max_concurrent_grabs",
            &cameraMaxConcurrentGrabs, 1.0f, 1,
            static_cast<int>(kHardwareCameraCount), "%d 路"))
        {
            cameraMaxConcurrentGrabs = std::clamp(cameraMaxConcurrentGrabs, 1,
                static_cast<int>(kHardwareCameraCount));
            HardwareRuntimeService::SetCameraMaxConcurrentGrabs(
                cameraMaxConcurrentGrabs);
            hardwareSettingsChanged = true;
        }
        ImGui::SetItemTooltip(
            "限制同时进入相机 SDK 的触发、取帧和重连数量；16 路高分辨率建议从 2~4 开始");

        PropertyRow("等待帧内存");
        ImGui::TextDisabled("%.1f MiB（应接近 0）",
            static_cast<double>(snapshot.cameraPendingFrameBytes) /
                (1024.0 * 1024.0));

        PropertyRow("设备扫描");
        ImGui::BeginDisabled(cameraBackend < 5);
        if (ImGui::Button("扫描设备##camera_scan"))
        {
            const char* discoveryBackends[] = {
                "", "", "", "", "", "mvs", "huaray"
            };
            cameraDiscovery = HardwareRuntimeService::DiscoverCameras(
                discoveryBackends[std::clamp(cameraBackend, 0, 6)]);
            selectedDiscoveredCamera = -1;
        }
        if (!cameraDiscovery.operation.message.empty())
            ImGui::TextWrapped("%s", cameraDiscovery.operation.message.c_str());
        if (!cameraDiscovery.devices.empty())
        {
            const char* preview = selectedDiscoveredCamera >= 0 &&
                selectedDiscoveredCamera < static_cast<int>(cameraDiscovery.devices.size())
                ? cameraDiscovery.devices[static_cast<std::size_t>(selectedDiscoveredCamera)].selector.c_str()
                : "选择已发现相机";
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##camera_discovered_devices", preview))
            {
                for (int index = 0;
                    index < static_cast<int>(cameraDiscovery.devices.size()); ++index)
                {
                    const CameraDeviceInfo& device =
                        cameraDiscovery.devices[static_cast<std::size_t>(index)];
                    const std::string label = device.model + " | " +
                        device.serialNumber + " | " + device.ipAddress +
                        " | " + device.status;
                    if (ImGui::Selectable(label.c_str(), selectedDiscoveredCamera == index))
                    {
                        selectedDiscoveredCamera = index;
                        std::snprintf(cameraAddress, sizeof(selectedCamera.address),
                            "%s", device.selector.c_str());
                        hardwareSettingsChanged = true;
                    }
                }
                ImGui::EndCombo();
            }
            const CameraDeviceInfo& first = cameraDiscovery.devices.front();
            if (!first.runtimePath.empty())
                ImGui::TextWrapped("SDK: %s", first.runtimePath.c_str());
            if (!first.runtimeVersion.empty())
                ImGui::TextWrapped("SDK Version: %s", first.runtimeVersion.c_str());

            if ((cameraBackend == 5 || cameraBackend == 6) &&
                selectedDiscoveredCamera >= 0 &&
                selectedDiscoveredCamera < static_cast<int>(cameraDiscovery.devices.size()))
            {
                const CameraDeviceInfo& selectedDevice = cameraDiscovery.devices[
                    static_cast<std::size_t>(selectedDiscoveredCamera)];
                if (selectedDevice.transport == "GigE")
                {
                    ImGui::SeparatorText("GigE ForceIP");
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputText("##force_ip_address", forceIpAddress,
                        sizeof(forceIpAddress));
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputText("##force_ip_subnet", forceIpSubnet,
                        sizeof(forceIpSubnet));
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputText("##force_ip_gateway", forceIpGateway,
                        sizeof(forceIpGateway));
                    if (ImGui::Button("Apply ForceIP##camera_force_ip"))
                    {
                        forceIpOperation = HardwareRuntimeService::ForceCameraIp(
                            cameraBackend == 5 ? "mvs" : "huaray",
                            selectedDevice.selector, forceIpAddress,
                            forceIpSubnet, forceIpGateway);
                        if (forceIpOperation.success)
                        {
                            cameraDiscovery = HardwareRuntimeService::DiscoverCameras(
                                cameraBackend == 5 ? "mvs" : "huaray");
                            selectedDiscoveredCamera = -1;
                        }
                    }
                    if (!forceIpOperation.message.empty())
                        ImGui::TextWrapped("%s", forceIpOperation.message.c_str());
                }
            }
        }
        ImGui::EndDisabled();
        PropertyRow("相机地址");
        hardwareSettingsChanged |= ImGui::InputText("##camera_address", cameraAddress, sizeof(cameraAddress));
        ImGui::SetItemTooltip(cameraBackend >= 5
            ? (cameraBackend == 6
                ? "华睿相机 IP、用户 ID、枚举序号，或 key:厂商:序列号"
                : "海康 GigE 相机 IP、序列号、用户名称或枚举序号")
            : "相机索引（例如 0）或 RTSP/HTTP 视频流 URL");

        PropertyRow("采集后端");
        if (ImGui::Combo("##camera_backend", &cameraBackend, cameraBackends,
            static_cast<int>(std::size(cameraBackends))))
        {
            hardwareSettingsChanged = true;
            if (cameraBackend >= 5 && cameraExposure < 1.0f)
                cameraExposure = 10000.0f;
            else if (cameraBackend < 5 && cameraExposure > 5.0f)
                cameraExposure = -6.0f;
        }

        PropertyRow("图像方向");
        const char* cameraOrientations[] = {
            "原始方向", "顺时针 90°", "旋转 180°", "逆时针 90°", "水平镜像", "垂直镜像"};
        if (ImGui::Combo("##camera_orientation", &cameraOrientation,
            cameraOrientations, static_cast<int>(std::size(cameraOrientations))))
        {
            hardwareSettingsChanged = true;
            if (cameraConnected)
                HardwareRuntimeService::SetCameraOrientation(cameraOrientation);
        }

        PropertyRow("来源名称");
        hardwareSettingsChanged |= ImGui::InputText("##camera_source_name", cameraSourceName, sizeof(cameraSourceName));

        PropertyRow("抓帧超时");
        hardwareSettingsChanged |= ImGui::DragInt("##camera_timeout", &cameraTimeoutMs, 1.0f, 1, 10000, "%d ms");
        ImGui::SetItemTooltip("单位：毫秒");

        PropertyRow("抓帧间隔");
        hardwareSettingsChanged |= ImGui::DragInt("##camera_interval", &cameraIntervalMs, 1.0f, 1, 10000, "%d ms");
        ImGui::SetItemTooltip("单位：毫秒；循环触发时控制相机采集节奏");

        PropertyRow("曝光模式");
        if (ImGui::Checkbox("自动曝光##camera_auto_exposure", &cameraAutoExposure))
        {
            hardwareSettingsChanged = true;
            if (cameraConnected)
            {
                HardwareRuntimeService::SetCameraControl(
                    CameraControl::AutoExposure, cameraAutoExposure ? 1.0 : 0.0);
            }
        }

        PropertyRow("曝光值");
        ImGui::BeginDisabled(cameraAutoExposure);
        const bool vendorCamera = cameraBackend >= 5;
        if (ImGui::DragFloat("##camera_exposure", &cameraExposure,
            vendorCamera ? 100.0f : 0.1f,
            vendorCamera ? 1.0f : -13.0f,
            vendorCamera ? 1000000.0f : 5.0f,
            vendorCamera ? "%.0f us" : "%.2f"))
        {
            hardwareSettingsChanged = true;
            if (cameraConnected)
                HardwareRuntimeService::SetCameraControl(CameraControl::Exposure, cameraExposure);
        }
        ImGui::EndDisabled();

        PropertyRow("增益");
        if (ImGui::DragFloat("##camera_gain", &cameraGain, 0.5f, 0.0f, 100.0f, "%.1f"))
        {
            hardwareSettingsChanged = true;
            if (cameraConnected)
                HardwareRuntimeService::SetCameraControl(CameraControl::Gain, cameraGain);
        }

        PropertyRow("触发模式");
        if (ImGui::Checkbox("PTP 时间同步##camera_ptp", &cameraPtpEnabled))
        {
            hardwareSettingsChanged = true;
            if (cameraConnected)
            {
                LogOperation("相机 PTP 配置",
                    HardwareRuntimeService::ConfigureCameraPtp(cameraPtpEnabled));
            }
        }
        ImGui::SetItemTooltip("GigE IEEE 1588 精密时间协议；需要相机和网卡支持");
        const char* triggerModes[] = {"连续采集", "软件触发", "Line1", "Line2"};
        if (ImGui::Combo("##camera_trigger_mode", &cameraTriggerMode,
            triggerModes, static_cast<int>(std::size(triggerModes))))
        {
            hardwareSettingsChanged = true;
            if (cameraConnected)
            {
                CameraTriggerConfig trigger;
                trigger.mode = static_cast<CameraTriggerMode>(
                    std::clamp(cameraTriggerMode, 0, 3));
                trigger.delayMicroseconds = cameraTriggerDelay;
                LogOperation("相机触发配置",
                    HardwareRuntimeService::ConfigureCameraTrigger(trigger));
            }
        }

        PropertyRow("触发延时");
        if (ImGui::DragFloat("##camera_trigger_delay", &cameraTriggerDelay,
            1.0f, 0.0f, 1000000.0f, "%.0f us"))
        {
            hardwareSettingsChanged = true;
            if (cameraConnected)
            {
                CameraTriggerConfig trigger;
                trigger.mode = static_cast<CameraTriggerMode>(
                    std::clamp(cameraTriggerMode, 0, 3));
                trigger.delayMicroseconds = cameraTriggerDelay;
                LogOperation("相机触发配置",
                    HardwareRuntimeService::ConfigureCameraTrigger(trigger));
            }
        }

        PropertyRow("采集模式");
        if (ImGui::Checkbox("自动抓帧##camera_auto_capture", &cameraAutoCapture))
        {
            hardwareSettingsChanged = true;
            if (cameraConnected)
                HardwareRuntimeService::SetCameraAutoCapture(cameraAutoCapture);
        }

        PropertyRow("帧缓存策略");
        const char* bufferPolicies[] = {"顺序帧", "仅最新帧"};
        ImGui::BeginDisabled(cameraBackend != 5);
        if (ImGui::Combo("##camera_buffer_policy", &cameraBufferPolicy,
            bufferPolicies, static_cast<int>(std::size(bufferPolicies))))
        {
            hardwareSettingsChanged = true;
            if (cameraConnected)
            {
                LogOperation("相机帧缓存策略",
                    HardwareRuntimeService::ConfigureCameraBufferPolicy(
                        static_cast<CameraBufferPolicy>(
                            std::clamp(cameraBufferPolicy, 0, 1))));
            }
        }
        ImGui::EndDisabled();

        PropertyRow("执行联动");
        hardwareSettingsChanged |= ImGui::Checkbox("抓帧后执行##camera_run_after", &cameraRunAfterCapture);
        if (!IsNarrowPanel())
            ImGui::SameLine();
        if (ImGui::Checkbox("执行前触发##camera_trigger_before", &cameraTriggerBeforeRun))
        {
            hardwareSettingsChanged = true;
            if (cameraConnected)
                HardwareRuntimeService::SetCameraTriggerOnInspection(cameraTriggerBeforeRun);
        }

        PropertyRow("启动连接");
        hardwareSettingsChanged |= ImGui::Checkbox(
            "程序启动时自动连接此槽##camera_connect_on_startup",
            &cameraConnectOnStartup);

        PropertyRow("断线重连");
        hardwareSettingsChanged |= ImGui::Checkbox(
            "自动重连##camera_auto_reconnect", &cameraAutoReconnect);

        PropertyRow("失败阈值");
        hardwareSettingsChanged |= ImGui::DragInt(
            "##camera_reconnect_threshold", &cameraReconnectFailureThreshold,
            1.0f, 1, 100, "%d 次");

        PropertyRow("重连退避");
        hardwareSettingsChanged |= ImGui::DragInt(
            "##camera_reconnect_initial", &cameraReconnectInitialDelayMs,
            10.0f, 1, 60000, "%d ms");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        hardwareSettingsChanged |= ImGui::DragInt(
            "##camera_reconnect_max", &cameraReconnectMaxDelayMs,
            50.0f, cameraReconnectInitialDelayMs, 60000, "最大 %d ms");
        ImGui::EndTable();
    }

    const float cameraButtonWidth = narrowPanel ? -1.0f : twoButtonWidth;
    if (ImGui::Button(cameraConnected ? "重新连接" : "连接相机",
        ImVec2(cameraButtonWidth, kActionButtonHeight)))
    {
        static const char* backendValues[] = {"", "dshow", "msmf", "ffmpeg", "gstreamer", "mvs", "huaray"};
        HardwareCameraConnectionConfig config;
        config.slotIndex = selectedCameraIndex;
        config.endpoint.address = cameraAddress;
        config.endpoint.resource = backendValues[std::clamp(cameraBackend, 0, 6)];
        config.endpoint.timeoutMs = std::max(1, cameraTimeoutMs);
        config.sourceName = cameraSourceName;
        config.grabTimeoutMs = std::max(1, cameraTimeoutMs);
        config.captureIntervalMs = std::max(1, cameraIntervalMs);
        config.orientation = std::clamp(cameraOrientation, 0, 5);
        config.autoCapture = cameraAutoCapture;
        config.triggerOnInspection = cameraTriggerBeforeRun;
        config.autoExposure = cameraAutoExposure;
        config.exposure = cameraExposure;
        config.gain = cameraGain;
        config.trigger.mode = static_cast<CameraTriggerMode>(
            std::clamp(cameraTriggerMode, 0, 3));
        config.trigger.delayMicroseconds = cameraTriggerDelay;
        config.bufferPolicy = static_cast<CameraBufferPolicy>(
            std::clamp(cameraBufferPolicy, 0, 1));
        config.ptpEnabled = cameraPtpEnabled;
        config.autoReconnect = cameraAutoReconnect;
        config.reconnectFailureThreshold = cameraReconnectFailureThreshold;
        config.reconnectInitialDelayMs = cameraReconnectInitialDelayMs;
        config.reconnectMaxDelayMs = cameraReconnectMaxDelayMs;
        const DeviceOperationResult result = HardwareRuntimeService::ConnectCamera(config);
        if (result.success)
        {
            connectedCameraIndex = selectedCameraIndex;
            cameraConnectOnStartup = true;
            hardwareSettingsChanged = true;
        }
        LogOperation("工业相机连接", result);
    }
    if (!narrowPanel)
        ImGui::SameLine();
    ImGui::BeginDisabled(!cameraConnected);
    if (ImGui::Button(cameraRunAfterCapture ? "抓帧并执行" : "抓取一帧",
        ImVec2(cameraButtonWidth, kActionButtonHeight)))
    {
        HardwareRuntimeService::RequestCameraFrame(cameraRunAfterCapture);
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!cameraConnected);
    if (ImGui::Button("断开相机", ImVec2(-1.0f, kActionButtonHeight)))
    {
        HardwareRuntimeService::DisconnectCameraSlot(selectedCameraIndex);
        connectedCameraIndex = -1;
        cameraConnectOnStartup = false;
        hardwareSettingsChanged = true;
    }
    ImGui::EndDisabled();
    DrawOperationMessage(snapshot.lastCameraOperation);
    }

    if (showArchive)
    {
    DrawSectionTitle("实时保存");
    bool archiveChanged = false;
    if (BeginPropertyTable("##archive_properties"))
    {
        PropertyRow("保存开关");
        archiveChanged |= ImGui::Checkbox("保存相机帧##archive_enabled", &archiveConfig.enabled);

        PropertyRow("保存目录");
        const float browseWidth = narrowPanel ? 30.0f : 74.0f;
        ImGui::SetNextItemWidth(std::max(80.0f,
            ImGui::GetContentRegionAvail().x - browseWidth - ImGui::GetStyle().ItemSpacing.x));
        archiveChanged |= ImGui::InputText("##archive_directory", archiveDirectory,
            sizeof(archiveDirectory));
        ImGui::SameLine();
        if (ImGui::Button(narrowPanel ? "..." : "浏览...", ImVec2(browseWidth, 0.0f)))
        {
            const std::string directory = OpenFolderDialog();
            if (!directory.empty())
            {
                std::snprintf(archiveDirectory, sizeof(archiveDirectory), "%s", directory.c_str());
                archiveChanged = true;
            }
        }
        ImGui::SetItemTooltip("选择实时保存目录");

        const char* archiveFormats[] = {"JPEG", "PNG", "BMP"};
        int archiveFormat = static_cast<int>(archiveConfig.format);
        PropertyRow("图像格式");
        if (ImGui::Combo("##archive_format", &archiveFormat, archiveFormats,
            static_cast<int>(std::size(archiveFormats))))
        {
            archiveConfig.format = static_cast<FrameArchiveFormat>(archiveFormat);
            archiveChanged = true;
        }

        PropertyRow("保存频率");
        archiveChanged |= ImGui::DragInt("##archive_every_n", &archiveConfig.saveEveryN,
            1.0f, 1, 10000, "每 %d 帧");
        ImGui::SetItemTooltip("每 N 帧保存一张；1 表示每帧保存");

        PropertyRow("JPEG 质量");
        ImGui::BeginDisabled(archiveConfig.format != FrameArchiveFormat::Jpeg);
        archiveChanged |= ImGui::SliderInt("##archive_jpeg_quality", &archiveConfig.jpegQuality, 20, 100);
        ImGui::EndDisabled();

        PropertyRow("缓冲队列");
        archiveChanged |= ImGui::SliderInt("##archive_queue", &archiveConfig.maxQueue, 1, 32);
        ImGui::SetItemTooltip("磁盘写入落后时使用的最大帧数，满后丢弃最旧帧");
        ImGui::EndTable();
    }

    if (archiveChanged)
    {
        archiveConfig.directory = archiveDirectory;
        FrameArchiveService::Configure(archiveConfig, true);
        archiveConfig = FrameArchiveService::Config();
    }

    const FrameArchiveSnapshot archive = FrameArchiveService::Snapshot();
    ImGui::TextDisabled("已保存 %llu · 待写 %zu · 丢弃 %llu · 失败 %llu",
        static_cast<unsigned long long>(archive.savedFrames), archive.pendingFrames,
        static_cast<unsigned long long>(archive.droppedFrames),
        static_cast<unsigned long long>(archive.failedFrames));
    if (!archive.lastError.empty())
        ImGui::TextColored(ImVec4(0.95f, 0.38f, 0.32f, 1.0f), "%s", archive.lastError.c_str());
    else if (!archive.lastSavedPath.empty())
    {
        ImGui::TextDisabled("最近保存");
        ImGui::TextWrapped("%s", archive.lastSavedPath.c_str());
    }
    ImGui::TextDisabled("设置文件");
    ImGui::TextWrapped("%s", FrameArchiveService::SettingsPath().c_str());
    }

    if (showOutput)
    {
    DrawSectionTitle("主输出通道（唯一可启用 PLC 握手）");
    ImGui::TextColored(ConnectionStateColor(snapshot.outputState), "%s%s%s",
        ConnectionStateName(snapshot.outputState),
        snapshot.outputAdapterName.empty() ? "" : " · ",
        snapshot.outputAdapterName.c_str());
    if (!snapshot.outputAdapterKey.empty())
        ImGui::TextDisabled("适配器: %s", snapshot.outputAdapterKey.c_str());
    ImGui::TextDisabled("发送队列 %zu%s · 成功 %llu · 失败 %llu · 丢弃 %llu",
        snapshot.outputQueueDepth, snapshot.outputQueueBusy ? " · 发送中" : "",
        static_cast<unsigned long long>(snapshot.outputSentCount),
        static_cast<unsigned long long>(snapshot.outputFailedCount),
        static_cast<unsigned long long>(snapshot.outputDroppedCount));
    if (snapshot.handshakeEnabled)
    {
        const double nowMs = std::chrono::duration<double, std::milli>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const double communicationAgeSeconds = snapshot.outputLastCommunicationTimestampMs > 0.0
            ? (std::max)(0.0,
                (nowMs - snapshot.outputLastCommunicationTimestampMs) / 1000.0)
            : -1.0;
        ImGui::TextColored(snapshot.outputCommunicationAlarm
            ? ImVec4(0.95f, 0.30f, 0.28f, 1.0f)
            : ImVec4(0.25f, 0.80f, 0.42f, 1.0f),
            "%s · 最后通讯 %s · 连续失败 %d",
            snapshot.outputCommunicationAlarm ? "通讯报警" : "通讯正常",
            communicationAgeSeconds < 0.0 ? "暂无"
                : (communicationAgeSeconds < 1.0 ? "刚刚" : "见悬停详情"),
            snapshot.outputConsecutiveFailures);
        if (communicationAgeSeconds >= 1.0)
            ImGui::SetItemTooltip("最后一次成功通讯距今 %.1f 秒", communicationAgeSeconds);
        if (snapshot.outputReconnecting)
        {
            ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.22f, 1.0f),
                "自动重连中 · 第 %d 次 · 等待 %d ms",
                snapshot.outputReconnectAttempts,
                snapshot.outputReconnectDelayMs);
        }
        if (snapshot.handshakeActive)
        {
            ImGui::TextColored(ImVec4(0.32f, 0.72f, 0.95f, 1.0f),
                "握手进行中 · %s%s",
                snapshot.handshakeTaskGroupName.empty()
                    ? "未指定任务" : snapshot.handshakeTaskGroupName.c_str(),
                snapshot.handshakeAwaitingAcknowledge
                    ? " · 等待 PLC ACK" : "");
        }
        if (snapshot.handshakeIgnoredTriggerCount > 0)
        {
            ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.22f, 1.0f),
                "Busy 期间已忽略 %llu 次 Trigger · 不会排队补跑",
                static_cast<unsigned long long>(
                    snapshot.handshakeIgnoredTriggerCount));
        }
        if (snapshot.handshakeAlarm)
        {
            ImGui::TextColored(ImVec4(0.95f, 0.30f, 0.28f, 1.0f),
                "握手报警 · %s", snapshot.handshakeAlarmMessage.c_str());
        }
    }

    const char* outputTypes[] = {
        "Modbus TCP｜线圈 IO（支持 PLC 握手）",
        "Modbus TCP｜标签映射（单结果输出）",
        "OPC UA｜NodeId 节点（单结果输出）",
        "TCP Socket｜PASS / FAIL 文本"};
    const char* outputTypeDescriptions[] = {
        "多信号模式：支持每个任务独立的 Trigger、OK、NG，以及公共 Busy、Done、Error、Heartbeat、ACK。",
        "单结果模式：将检测结果写入一个逻辑标签，底层可使用线圈或保持寄存器；不支持多信号握手。",
        "单结果模式：将检测结果写入指定 OPC UA NodeId；不支持 PLC IO 握手。",
        "文本模式：向 TCP Server 发送 PASS / FAIL 字符串；不支持 PLC IO 握手。"};
    const int previousOutputType = outputType;
    if (BeginPropertyTable("##output_properties"))
    {
        PropertyRow("输出类型");
        hardwareSettingsChanged |= ImGui::Combo("##output_type", &outputType, outputTypes,
            static_cast<int>(std::size(outputTypes)));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImVec4(0.38f, 0.72f, 0.78f, 1.0f), "用途：%s",
            outputTypeDescriptions[std::clamp(outputType, 0, 3)]);
        ImGui::PopTextWrapPos();
        if (outputType != previousOutputType)
        {
            if (outputType == 2 && outputPort == 502)
                outputPort = 4840;
            else if (outputType != 2 && outputPort == 4840)
                outputPort = 502;
            std::snprintf(outputResource, sizeof(outputResource), "%s",
                outputType == 2 || outputType == 3 ? "" : "1");
        }

        PropertyRow("适配器标识");
        hardwareSettingsChanged |= ImGui::InputText("##output_key", outputKey, sizeof(outputKey));

        PropertyRow("主机地址");
        hardwareSettingsChanged |= ImGui::InputText("##output_address", outputAddress, sizeof(outputAddress));
        ImGui::SetItemTooltip(outputType == 2
            ? "OPC UA 主机或完整 opc.tcp:// URL"
            : outputType == 3 ? "TCP Server 主机或 IP" : "PLC/Modbus TCP 主机或 IP");

        PropertyRow("端口");
        hardwareSettingsChanged |= ImGui::DragInt("##output_port", &outputPort, 1.0f, 0, 65535);

        if (outputType != 3)
        {
            PropertyRow(outputType == 2 ? "端点路径" : "Unit ID");
            hardwareSettingsChanged |= ImGui::InputText("##output_resource", outputResource, sizeof(outputResource));
        }

        PropertyRow("连接超时");
        hardwareSettingsChanged |= ImGui::DragInt("##output_timeout", &outputTimeoutMs, 10.0f, 1, 60000, "%d ms");
        ImGui::SetItemTooltip("单位：毫秒");

        if (outputType == 1 || outputType == 2)
        {
            PropertyRow(outputType == 1 ? "PLC 标签" : "NodeId");
            hardwareSettingsChanged |= ImGui::InputText("##output_target", outputTarget, sizeof(outputTarget));
            ImGui::SetItemTooltip(outputType == 1 ? "PLC 标签名" : "例如 ns=2;s=Inspection.OK");
        }

        if (outputType == 0 || outputType == 1)
        {
            PropertyRow(outputType == 0 ? "线圈地址" : "映射地址");
            hardwareSettingsChanged |= ImGui::DragInt("##output_mapping_address", &outputAddressValue, 1.0f, 0, 65535);
            ImGui::SetItemTooltip("协议地址从 0 开始；PLC 显示 00001 时通常填写 0");
        }

        if (outputType == 3)
        {
            hardwareSettingsChanged |= ImGui::Checkbox(
                "发送二维码序列号 JSON##tcp_qr_json", &tcpSendQrJson);
            ImGui::SetItemTooltip(
                "发送 {result, serial, serials} JSON；序列号来自二维码/条码识别工具");
            PropertyRow("Pass 文本");
            ImGui::BeginDisabled(tcpSendQrJson);
            hardwareSettingsChanged |= ImGui::InputText("##tcp_pass_text", tcpPassText, sizeof(tcpPassText));
            PropertyRow("Fail 文本");
            hardwareSettingsChanged |= ImGui::InputText("##tcp_fail_text", tcpFailText, sizeof(tcpFailText));
            ImGui::EndDisabled();
        }

        PropertyRow("输出选项");
        if (outputType == 1)
        {
            hardwareSettingsChanged |= ImGui::Checkbox("保持寄存器##plc_holding", &plcHoldingRegister);
            ImGui::SameLine();
        }
        if (outputType == 3)
        {
            hardwareSettingsChanged |= ImGui::Checkbox("追加 CRLF##tcp_crlf", &tcpAppendCrLf);
            ImGui::SameLine();
        }
        hardwareSettingsChanged |= ImGui::Checkbox("反相##output_invert", &outputInvert);

        PropertyRow("自动发布");
        ImGui::BeginDisabled(outputHandshakeEnabled);
        if (ImGui::Checkbox("批次完成后发布##output_auto_publish", &outputAutoPublish))
        {
            hardwareSettingsChanged = true;
            if (snapshot.outputState == DeviceConnectionState::Connected)
                HardwareRuntimeService::SetOutputAutoPublish(outputAutoPublish);
        }
        ImGui::EndDisabled();
        if (outputHandshakeEnabled)
            ImGui::SetItemTooltip("启用 PLC IO 握手后由 OK/NG/Error/Done 信号发布结果");

        PropertyRow("发送队列");
        hardwareSettingsChanged |= ImGui::DragInt(
            "##output_queue_size", &outputQueueSize, 1.0f, 1, 1024, "%d 条");

        PropertyRow("失败重试");
        hardwareSettingsChanged |= ImGui::DragInt(
            "##output_retry_count", &outputRetryCount, 1.0f, 0, 10, "%d 次");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        hardwareSettingsChanged |= ImGui::DragInt(
            "##output_retry_delay", &outputRetryDelayMs, 10.0f, 1, 60000, "%d ms");

        PropertyRow("重试重连");
        hardwareSettingsChanged |= ImGui::Checkbox(
            "发送失败时重连##output_reconnect_retry", &outputReconnectBeforeRetry);
        ImGui::EndTable();
    }

    DrawSectionTitle("辅助结果输出（3 路可同时在线）");
    ImGui::TextDisabled(
        "辅助通道不接收 Trigger/ACK，只在批次完成后同步发送 PASS/FAIL；主握手通道保持独立。" );
    const std::vector<HardwareAuxiliaryOutputSnapshot> auxiliarySnapshots =
        HardwareRuntimeService::AuxiliaryOutputSnapshots();
    auto auxiliarySnapshotFor = [&auxiliarySnapshots](const char* key)
        -> const HardwareAuxiliaryOutputSnapshot*
    {
        const auto found = std::find_if(auxiliarySnapshots.begin(),
            auxiliarySnapshots.end(), [key](const HardwareAuxiliaryOutputSnapshot& item)
            {
                return item.adapterKey == key;
            });
        return found == auxiliarySnapshots.end() ? nullptr : &*found;
    };

    if (ImGui::BeginTable("##auxiliary_output_slots", 3,
        ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX))
    {
        for (int index = 0; index < static_cast<int>(auxiliaryOutputStates.size()); ++index)
        {
            ImGui::TableNextColumn();
            ImGui::PushID(index);
            const AuxiliaryOutputUiState& item = auxiliaryOutputStates[index];
            const HardwareAuxiliaryOutputSnapshot* state =
                auxiliarySnapshotFor(item.key);
            const bool online = state &&
                state->state == DeviceConnectionState::Connected;
            if (selectedAuxiliaryOutput == index)
                ImGui::PushStyleColor(ImGuiCol_Button,
                    ImVec4(0.08f, 0.40f, 0.46f, 1.0f));
            char label[64];
            std::snprintf(label, sizeof(label), "辅助通道 %d%s", index + 1,
                online ? " · 在线" : (item.enabled ? " · 已启用" : " · 停用"));
            if (ImGui::Button(label, ImVec2(-1.0f, 32.0f)))
                selectedAuxiliaryOutput = index;
            if (selectedAuxiliaryOutput == index)
                ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    AuxiliaryOutputUiState& auxiliary = auxiliaryOutputStates[
        static_cast<std::size_t>(selectedAuxiliaryOutput)];
    const HardwareAuxiliaryOutputSnapshot* auxiliarySnapshot =
        auxiliarySnapshotFor(auxiliary.key);
    const bool auxiliaryConnected = auxiliarySnapshot &&
        auxiliarySnapshot->state == DeviceConnectionState::Connected;
    ImGui::TextColored(ConnectionStateColor(auxiliaryConnected
        ? DeviceConnectionState::Connected : DeviceConnectionState::Disconnected),
        "辅助通道 %d：%s", selectedAuxiliaryOutput + 1,
        auxiliaryConnected ? "已连接" : "未连接");

    const char* auxiliaryTypes[] = {
        "Modbus TCP｜单线圈结果",
        "Modbus TCP｜标签映射结果",
        "OPC UA｜NodeId 结果",
        "TCP Socket｜PASS / FAIL 文本"};
    if (BeginPropertyTable("##auxiliary_output_properties"))
    {
        PropertyRow("启用通道");
        if (ImGui::Checkbox("启用并参与同步输出##aux_enabled", &auxiliary.enabled))
        {
            hardwareSettingsChanged = true;
            if (!auxiliary.enabled && auxiliaryConnected)
                HardwareRuntimeService::DisconnectAuxiliaryOutput(auxiliary.key);
        }

        PropertyRow("输出类型");
        hardwareSettingsChanged |= ImGui::Combo("##aux_type", &auxiliary.type,
            auxiliaryTypes, static_cast<int>(std::size(auxiliaryTypes)));

        PropertyRow("适配器标识");
        ImGui::BeginDisabled(auxiliaryConnected);
        hardwareSettingsChanged |= ImGui::InputText(
            "##aux_key", auxiliary.key, sizeof(auxiliary.key));
        ImGui::EndDisabled();
        if (auxiliaryConnected)
            ImGui::SetItemTooltip("请先断开该辅助通道再修改标识");

        PropertyRow("主机地址");
        hardwareSettingsChanged |= ImGui::InputText(
            "##aux_address", auxiliary.address, sizeof(auxiliary.address));

        PropertyRow("端口");
        hardwareSettingsChanged |= ImGui::DragInt(
            "##aux_port", &auxiliary.port, 1.0f, 0, 65535);

        if (auxiliary.type == 0 || auxiliary.type == 1)
        {
            PropertyRow("Unit ID");
            hardwareSettingsChanged |= ImGui::InputText(
                "##aux_resource", auxiliary.resource, sizeof(auxiliary.resource));
        }
        if (auxiliary.type == 1 || auxiliary.type == 2)
        {
            PropertyRow(auxiliary.type == 1 ? "PLC 标签" : "NodeId");
            hardwareSettingsChanged |= ImGui::InputText(
                "##aux_target", auxiliary.target, sizeof(auxiliary.target));
        }
        if (auxiliary.type == 0 || auxiliary.type == 1)
        {
            PropertyRow(auxiliary.type == 0 ? "线圈地址" : "映射地址");
            hardwareSettingsChanged |= ImGui::DragInt(
                "##aux_mapping_address", &auxiliary.mappingAddress,
                1.0f, 0, 65535);
        }
        if (auxiliary.type == 3)
        {
            hardwareSettingsChanged |= ImGui::Checkbox(
                "发送二维码序列号 JSON##aux_qr_json", &auxiliary.sendQrJson);
            ImGui::SetItemTooltip(
                "发送 {result, serial, serials} JSON；序列号来自二维码/条码识别工具");
            PropertyRow("Pass 文本");
            ImGui::BeginDisabled(auxiliary.sendQrJson);
            hardwareSettingsChanged |= ImGui::InputText(
                "##aux_pass", auxiliary.passText, sizeof(auxiliary.passText));
            PropertyRow("Fail 文本");
            hardwareSettingsChanged |= ImGui::InputText(
                "##aux_fail", auxiliary.failText, sizeof(auxiliary.failText));
            ImGui::EndDisabled();
        }

        PropertyRow("连接超时");
        hardwareSettingsChanged |= ImGui::DragInt(
            "##aux_timeout", &auxiliary.timeoutMs, 10.0f, 1, 60000, "%d ms");

        PropertyRow("输出选项");
        if (auxiliary.type == 1)
        {
            hardwareSettingsChanged |= ImGui::Checkbox(
                "保持寄存器##aux_holding", &auxiliary.plcHoldingRegister);
            ImGui::SameLine();
        }
        if (auxiliary.type == 3)
        {
            hardwareSettingsChanged |= ImGui::Checkbox(
                "追加 CRLF##aux_crlf", &auxiliary.appendCrLf);
            ImGui::SameLine();
        }
        hardwareSettingsChanged |= ImGui::Checkbox(
            "反相##aux_invert", &auxiliary.invert);
        ImGui::SameLine();
        hardwareSettingsChanged |= ImGui::Checkbox(
            "批次完成后同步发送##aux_publish", &auxiliary.autoPublish);
        ImGui::EndTable();
    }

    ImGui::BeginDisabled(!auxiliary.enabled);
    if (ImGui::Button(auxiliaryConnected ? "重新连接辅助通道" : "连接辅助通道",
        ImVec2(twoButtonWidth, kActionButtonHeight)))
    {
        DeviceOperationResult result = HardwareRuntimeService::ConnectAuxiliaryOutput(
            BuildAuxiliaryOutputConfig(auxiliary));
        auxiliaryOutputOperations[static_cast<std::size_t>(
            selectedAuxiliaryOutput)] = result;
        LogOperation("辅助输出连接", result);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!auxiliaryConnected);
    if (ImGui::Button("断开辅助通道", ImVec2(-1.0f, kActionButtonHeight)))
    {
        HardwareRuntimeService::DisconnectAuxiliaryOutput(auxiliary.key);
        auxiliaryOutputOperations[static_cast<std::size_t>(
            selectedAuxiliaryOutput)] = {true, "辅助输出已断开"};
    }
    ImGui::EndDisabled();
    if (auxiliarySnapshot && !auxiliarySnapshot->lastOperation.message.empty())
        DrawOperationMessage(auxiliarySnapshot->lastOperation);
    else
        DrawOperationMessage(auxiliaryOutputOperations[static_cast<std::size_t>(
            selectedAuxiliaryOutput)]);

    }

    if (showPlc)
    {
    if (outputType == 0)
    {
        DrawSectionTitle("PLC IO 映射与握手");
        bool handshakeSettingsChanged = false;
        if (ImGui::Checkbox("启用工业握手##plc_handshake_enabled",
            &outputHandshakeEnabled))
        {
            handshakeSettingsChanged = true;
            if (outputHandshakeEnabled)
                outputAutoPublish = false;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Trigger → Busy → Done/OK/NG/Error → ACK");

        if (BeginPropertyTable("##plc_handshake_properties"))
        {
            PropertyRow("轮询周期");
            handshakeSettingsChanged |= ImGui::DragInt(
                "##plc_poll_interval", &outputPollIntervalMs,
                1.0f, 10, 5000, "%d ms");

            PropertyRow("ACK 超时");
            handshakeSettingsChanged |= ImGui::DragInt(
                "##plc_ack_timeout", &outputAcknowledgementTimeoutMs,
                10.0f, 100, 60000, "%d ms");

            PropertyRow("检测超时");
            handshakeSettingsChanged |= ImGui::DragInt(
                "##plc_inspection_timeout", &outputInspectionTimeoutMs,
                100.0f, 1000, 600000, "%d ms");

            PropertyRow("心跳周期");
            handshakeSettingsChanged |= ImGui::DragInt(
                "##plc_heartbeat_interval", &outputHeartbeatIntervalMs,
                10.0f, 100, 60000, "%d ms");

            PropertyRow("断线恢复");
            handshakeSettingsChanged |= ImGui::Checkbox(
                "自动重连##plc_auto_reconnect", &outputAutoReconnect);

            PropertyRow("失败阈值");
            handshakeSettingsChanged |= ImGui::DragInt(
                "##plc_reconnect_threshold", &outputReconnectFailureThreshold,
                1.0f, 1, 100, "%d 次");

            PropertyRow("重连退避");
            ImGui::SetNextItemWidth((ImGui::GetContentRegionAvail().x -
                ImGui::GetStyle().ItemSpacing.x) * 0.5f);
            handshakeSettingsChanged |= ImGui::DragInt(
                "##plc_reconnect_initial", &outputReconnectInitialDelayMs,
                10.0f, 1, 60000, "%d ms");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            handshakeSettingsChanged |= ImGui::DragInt(
                "##plc_reconnect_max", &outputReconnectMaxDelayMs,
                10.0f, 1, 60000, "%d ms");
            ImGui::EndTable();
        }

        ImGui::TextDisabled("IO 映射（每个 Trigger 可绑定一个独立任务）");
        if (currentTaskGroupNames.empty())
        {
            manualTriggerTask.clear();
        }
        else if (ToolChainState::TaskGroupIndexByName(manualTriggerTask) < 0)
        {
            manualTriggerTask = currentTaskGroupNames.front();
        }
        ImGui::AlignTextToFramePadding();
        ImGui::Text("当前任务");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220.0f);
        DrawTaskSlotCombo("##plc_mapping_task_filter", manualTriggerTask);
        ImGui::SameLine();
        ImGui::TextDisabled("下方分别显示任务独立信号和公共握手信号");

        const auto isTaskScopedMapping = [](const HardwareIoMapping& mapping)
        {
            return mapping.signal == HardwareIoSignal::Trigger ||
                mapping.signal == HardwareIoSignal::Ok ||
                mapping.signal == HardwareIoSignal::Ng;
        };
        int removeMapping = -1;
        for (int mappingSection = 0; mappingSection < 2; ++mappingSection)
        {
        const bool showTaskMappings = mappingSection == 0;
        ImGui::SeparatorText(showTaskMappings
            ? "当前任务信号" : "公共握手信号");
        const char* tableId = showTaskMappings
            ? "##plc_task_io_mapping" : "##plc_common_io_mapping";
        const float tableHeight = showTaskMappings ? 190.0f : 240.0f;
        if (ImGui::BeginTable(tableId, 9,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollX | ImGuiTableFlags_SizingFixedFit,
            ImVec2(0.0f, tableHeight)))
        {
            ImGui::TableSetupColumn("启用", ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn("信号", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("方向", ImGuiTableColumnFlags_WidthFixed, 132.0f);
            ImGui::TableSetupColumn("地址", ImGuiTableColumnFlags_WidthFixed, 72.0f);
            ImGui::TableSetupColumn("反相", ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn("脉冲", ImGuiTableColumnFlags_WidthFixed, 82.0f);
            ImGui::TableSetupColumn("任务", ImGuiTableColumnFlags_WidthFixed, 132.0f);
            ImGui::TableSetupColumn("单点测试", ImGuiTableColumnFlags_WidthFixed, 112.0f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 34.0f);
            ImGui::TableHeadersRow();

            const char* signalItems[] = {
                "触发 Trigger", "运行 Busy", "完成 Done", "合格 OK",
                "不合格 NG", "异常 Error", "心跳 Heartbeat", "确认 ACK"};
            const char* directionItems[] = {"PLC → 视觉", "视觉 → PLC"};
            for (std::size_t index = 0; index < outputIoMappings.size(); ++index)
            {
                HardwareIoMapping& mapping = outputIoMappings[index];
                const bool taskScoped = isTaskScopedMapping(mapping);
                if ((showTaskMappings && (!taskScoped ||
                    mapping.taskGroupName != manualTriggerTask)) ||
                    (!showTaskMappings && taskScoped))
                {
                    continue;
                }
                ImGui::PushID(static_cast<int>(index));
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                handshakeSettingsChanged |= ImGui::Checkbox("##enabled", &mapping.enabled);

                ImGui::TableSetColumnIndex(1);
                int signal = static_cast<int>(mapping.signal);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("##signal", &signal, signalItems,
                    static_cast<int>(std::size(signalItems))))
                {
                    mapping.signal = static_cast<HardwareIoSignal>(signal);
                    if (mapping.signal == HardwareIoSignal::Trigger ||
                        mapping.signal == HardwareIoSignal::Acknowledge)
                    {
                        mapping.direction = HardwareIoDirection::Input;
                    }
                    else
                    {
                        mapping.direction = HardwareIoDirection::Output;
                    }
                    if ((mapping.signal == HardwareIoSignal::Trigger ||
                        mapping.signal == HardwareIoSignal::Ok ||
                        mapping.signal == HardwareIoSignal::Ng) &&
                        mapping.taskGroupName.empty())
                    {
                        mapping.taskGroupName = manualTriggerTask;
                    }
                    handshakeSettingsChanged = true;
                }

                ImGui::TableSetColumnIndex(2);
                int direction = static_cast<int>(mapping.direction);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("##direction", &direction, directionItems,
                    static_cast<int>(std::size(directionItems))))
                {
                    mapping.direction = static_cast<HardwareIoDirection>(direction);
                    handshakeSettingsChanged = true;
                }

                ImGui::TableSetColumnIndex(3);
                int address = mapping.address;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::DragInt("##address", &address, 1.0f, 0, 65535))
                {
                    mapping.address = ClampAddress(address);
                    handshakeSettingsChanged = true;
                }

                ImGui::TableSetColumnIndex(4);
                handshakeSettingsChanged |= ImGui::Checkbox("##invert", &mapping.invert);

                ImGui::TableSetColumnIndex(5);
                ImGui::BeginDisabled(mapping.direction == HardwareIoDirection::Input);
                ImGui::SetNextItemWidth(-1.0f);
                handshakeSettingsChanged |= ImGui::DragInt(
                    "##pulse", &mapping.pulseMs, 10.0f, 0, 60000, "%d ms");
                ImGui::EndDisabled();

                ImGui::TableSetColumnIndex(6);
                const bool taskScopedMapping = isTaskScopedMapping(mapping);
                if (taskScopedMapping)
                {
                    ImGui::TextUnformatted(mapping.taskGroupName.c_str());
                }
                else
                {
                    ImGui::TextDisabled("公共");
                }

                ImGui::TableSetColumnIndex(7);
                ImGui::BeginDisabled(!outputConnected || outputConfigurationDirty ||
                    handshakeSettingsChanged);
                if (mapping.direction == HardwareIoDirection::Input)
                {
                    if (ImGui::SmallButton("读取"))
                        LogOperation(IoSignalName(mapping.signal),
                            HardwareRuntimeService::TestIoMapping(index, false));
                }
                else
                {
                    if (ImGui::SmallButton("ON"))
                        LogOperation(IoSignalName(mapping.signal),
                            HardwareRuntimeService::TestIoMapping(index, true));
                    ImGui::SameLine();
                    if (ImGui::SmallButton("OFF"))
                        LogOperation(IoSignalName(mapping.signal),
                            HardwareRuntimeService::TestIoMapping(index, false));
                }
                ImGui::EndDisabled();

                ImGui::TableSetColumnIndex(8);
                if (ImGui::SmallButton("×"))
                    removeMapping = static_cast<int>(index);
                if (mapping.signal == HardwareIoSignal::Trigger &&
                    mapping.taskGroupName == manualTriggerTask)
                {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                        ImGui::GetColorU32(ImVec4(0.10f, 0.38f, 0.42f, 0.42f)));
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        }
        if (removeMapping >= 0)
        {
            outputIoMappings.erase(outputIoMappings.begin() + removeMapping);
            handshakeSettingsChanged = true;
        }

        if (ImGui::Button("添加 IO"))
        {
            std::uint16_t nextAddress = 0;
            for (const HardwareIoMapping& mapping : outputIoMappings)
                nextAddress = (std::max)(nextAddress,
                    static_cast<std::uint16_t>(mapping.address +
                        (mapping.address < 65535 ? 1 : 0)));
            HardwareIoMapping mapping;
            mapping.address = nextAddress;
            mapping.taskGroupName = manualTriggerTask;
            outputIoMappings.push_back(std::move(mapping));
            handshakeSettingsChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("补齐任务 Trigger"))
        {
            const std::size_t before = outputIoMappings.size();
            if (HardwareSettingsService::EnsureTaskTriggerMappings(
                outputIoMappings, currentTaskGroupNames))
            {
                const std::size_t added = outputIoMappings.size() - before;
                triggerSyncMessage = "已新增 " + std::to_string(added) +
                    " 个任务 Trigger";
                handshakeSettingsChanged = true;
            }
            else
            {
                triggerSyncMessage = currentTaskGroupNames.empty()
                    ? "当前没有任务可同步" : "当前任务 Trigger 已完整";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("恢复标准映射"))
        {
            outputIoMappings = HardwareSettingsService::BuildStandardIoMappings(
                currentTaskGroupNames);
            triggerSyncMessage = "已按任务顺序恢复标准地址：任务01=0，任务02起=8...";
            handshakeSettingsChanged = true;
        }
        if (!triggerSyncMessage.empty())
            ImGui::TextDisabled("%s", triggerSyncMessage.c_str());
        ImGui::BeginDisabled(!outputConnected || !outputHandshakeEnabled ||
            manualTriggerTask.empty() || outputConfigurationDirty ||
            handshakeSettingsChanged);
        if (ImGui::Button("模拟触发：执行当前任务"))
        {
            LogOperation("PLC 单任务触发",
                HardwareRuntimeService::RequestTaskInspection(
                    manualTriggerTask, true));
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(!outputConnected || !outputHandshakeEnabled ||
            outputConfigurationDirty || handshakeSettingsChanged ||
            snapshot.handshakeActive);
        if (ImGui::Button("握手测试 Pass"))
            LogOperation("整套握手测试",
                HardwareRuntimeService::RequestHandshakeTest(ToolResultStatus::Pass));
        ImGui::SameLine();
        if (ImGui::Button("Fail"))
            LogOperation("整套握手测试",
                HardwareRuntimeService::RequestHandshakeTest(ToolResultStatus::Fail));
        ImGui::SameLine();
        if (ImGui::Button("Error"))
            LogOperation("整套握手测试",
                HardwareRuntimeService::RequestHandshakeTest(ToolResultStatus::Error));
        ImGui::EndDisabled();

        if (handshakeSettingsChanged)
        {
            hardwareSettingsChanged = true;
            outputConfigurationDirty = true;
        }
        if (outputConfigurationDirty)
        {
            ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.22f, 1.0f),
                "IO 或握手参数已修改，请点击“重新连接输出”后生效");
        }
        ImGui::TextDisabled(
            "单任务触发：仅空闲时接受；Busy/等待 ACK 期间忽略新 Trigger，不排队补跑。无相机时文件夹每轮推进一张。");
    }
    else
    {
        DrawSectionTitle("PLC IO 映射与握手");
        ImGui::TextDisabled("当前输出类型不是 Modbus TCP 线圈，请先在“检测结果输出”中切换输出类型。");
    }
    }

    if (showOutput || showPlc)
    {
    if (showOutput)
        ImGui::SeparatorText("主输出通道连接与测试");
    if (ImGui::Button(outputConnected ? "重新连接输出" : "连接输出",
        ImVec2(narrowPanel ? -1.0f : twoButtonWidth, kActionButtonHeight)))
    {
        HardwareOutputConnectionConfig config;
        config.adapterType = static_cast<HardwareOutputAdapterType>(std::clamp(outputType, 0, 3));
        config.endpoint.address = outputAddress;
        config.endpoint.port = ClampPort(outputPort);
        config.endpoint.resource = outputResource;
        config.endpoint.timeoutMs = std::max(1, outputTimeoutMs);
        config.binding.adapterKey = outputKey;
        config.binding.target = outputTarget;
        config.binding.address = ClampAddress(outputAddressValue);
        config.binding.invert = outputInvert;
        config.binding.passText = tcpPassText;
        config.binding.failText = tcpFailText;
        config.binding.sendQrJson = tcpSendQrJson;
        config.binding.appendCrLf = tcpAppendCrLf;
        config.plcUseHoldingRegister = plcHoldingRegister;
        config.autoPublish = outputAutoPublish;
        config.maxQueueSize = outputQueueSize;
        config.retryCount = outputRetryCount;
        config.retryDelayMs = outputRetryDelayMs;
        config.reconnectBeforeRetry = outputReconnectBeforeRetry;
        config.handshake.enabled = outputHandshakeEnabled;
        config.handshake.mappings = outputIoMappings;
        config.handshake.pollIntervalMs = outputPollIntervalMs;
        config.handshake.acknowledgementTimeoutMs =
            outputAcknowledgementTimeoutMs;
        config.handshake.inspectionTimeoutMs = outputInspectionTimeoutMs;
        config.handshake.heartbeatIntervalMs = outputHeartbeatIntervalMs;
        config.handshake.autoReconnect = outputAutoReconnect;
        config.handshake.reconnectFailureThreshold =
            outputReconnectFailureThreshold;
        config.handshake.reconnectInitialDelayMs =
            outputReconnectInitialDelayMs;
        config.handshake.reconnectMaxDelayMs = outputReconnectMaxDelayMs;
        const DeviceOperationResult result =
            HardwareRuntimeService::ConnectOutput(config);
        if (result.success)
            outputConfigurationDirty = false;
        LogOperation("硬件输出连接", result);
    }
    if (!narrowPanel)
        ImGui::SameLine();
    ImGui::BeginDisabled(!outputConnected);
    if (ImGui::Button("断开输出", ImVec2(narrowPanel ? -1.0f : twoButtonWidth, kActionButtonHeight)))
        HardwareRuntimeService::DisconnectOutput();
    ImGui::EndDisabled();

    ImGui::BeginDisabled(!outputConnected);
    if (ImGui::Button("测试 Pass", ImVec2(narrowPanel ? -1.0f : twoButtonWidth, kActionButtonHeight)))
        LogOperation("测试 Pass 输出", HardwareRuntimeService::PublishConfiguredStatus(ToolResultStatus::Pass));
    if (!narrowPanel)
        ImGui::SameLine();
    if (ImGui::Button("测试 Fail", ImVec2(narrowPanel ? -1.0f : twoButtonWidth, kActionButtonHeight)))
        LogOperation("测试 Fail 输出", HardwareRuntimeService::PublishConfiguredStatus(ToolResultStatus::Fail));
    ImGui::EndDisabled();
    DrawOperationMessage(snapshot.lastOutputOperation);
    }

    if (hardwareSettingsChanged)
    {
        HardwarePanelSettings settings;
        settings.activeCameraIndex = selectedCameraIndex;
        settings.cameraMaxConcurrentGrabs = cameraMaxConcurrentGrabs;
        settings.cameras.resize(kHardwareCameraCount);
        for (std::size_t index = 0; index < cameraStates.size(); ++index)
        {
            const CameraUiState& source = cameraStates[index];
            HardwareCameraSettings& target = settings.cameras[index];
            target.connectOnStartup = source.connectOnStartup;
            target.address = source.address;
            target.sourceName = source.sourceName;
            target.backend = source.backend;
            target.orientation = source.orientation;
            target.timeoutMs = source.timeoutMs;
            target.intervalMs = source.intervalMs;
            target.autoCapture = source.autoCapture;
            target.runAfterCapture = source.runAfterCapture;
            target.triggerBeforeRun = source.triggerBeforeRun;
            target.autoExposure = source.autoExposure;
            target.exposure = source.exposure;
            target.gain = source.gain;
            target.triggerMode = source.triggerMode;
            target.triggerDelayMicroseconds = source.triggerDelayMicroseconds;
            target.bufferPolicy = source.bufferPolicy;
            target.ptpEnabled = source.ptpEnabled;
            target.autoReconnect = source.autoReconnect;
            target.reconnectFailureThreshold = source.reconnectFailureThreshold;
            target.reconnectInitialDelayMs = source.reconnectInitialDelayMs;
            target.reconnectMaxDelayMs = source.reconnectMaxDelayMs;
        }
        settings.outputType = outputType;
        settings.outputKey = outputKey;
        settings.outputAddress = outputAddress;
        settings.outputPort = outputPort;
        settings.outputResource = outputResource;
        settings.outputTarget = outputTarget;
        settings.outputAddressValue = outputAddressValue;
        settings.outputTimeoutMs = outputTimeoutMs;
        settings.plcHoldingRegister = plcHoldingRegister;
        settings.tcpPassText = tcpPassText;
        settings.tcpFailText = tcpFailText;
        settings.tcpSendQrJson = tcpSendQrJson;
        settings.tcpAppendCrLf = tcpAppendCrLf;
        settings.outputInvert = outputInvert;
        settings.outputAutoPublish = outputAutoPublish;
        settings.outputQueueSize = outputQueueSize;
        settings.outputRetryCount = outputRetryCount;
        settings.outputRetryDelayMs = outputRetryDelayMs;
        settings.outputReconnectBeforeRetry = outputReconnectBeforeRetry;
        settings.outputHandshakeEnabled = outputHandshakeEnabled;
        settings.outputPollIntervalMs = outputPollIntervalMs;
        settings.outputAcknowledgementTimeoutMs =
            outputAcknowledgementTimeoutMs;
        settings.outputInspectionTimeoutMs = outputInspectionTimeoutMs;
        settings.outputHeartbeatIntervalMs = outputHeartbeatIntervalMs;
        settings.outputAutoReconnect = outputAutoReconnect;
        settings.outputReconnectFailureThreshold =
            outputReconnectFailureThreshold;
        settings.outputReconnectInitialDelayMs =
            outputReconnectInitialDelayMs;
        settings.outputReconnectMaxDelayMs = outputReconnectMaxDelayMs;
        settings.outputIoMappings = outputIoMappings;
        settings.auxiliaryOutputs.clear();
        settings.auxiliaryOutputs.reserve(auxiliaryOutputStates.size());
        for (const AuxiliaryOutputUiState& auxiliary : auxiliaryOutputStates)
            settings.auxiliaryOutputs.push_back(
                BuildAuxiliaryOutputConfig(auxiliary));
        if (HardwareSettingsService::Save(settings, {}, &hardwareSettingsError))
            LogSystem::Add(LOG_INFO,
                "event=config_modified subsystem=hardware path=%s",
                HardwareSettingsService::SettingsPath().c_str());
        else
            LogSystem::Add(LOG_ERROR,
                "event=config_save_failed subsystem=hardware error=%s",
                hardwareSettingsError.c_str());
    }

    ImGui::Spacing();
    ImGui::TextDisabled("设备参数：修改后自动保存");
    ImGui::TextWrapped("%s", HardwareSettingsService::SettingsPath().c_str());
    if (!hardwareSettingsError.empty())
        ImGui::TextColored(ImVec4(0.95f, 0.38f, 0.32f, 1.0f),
            "保存失败：%s", hardwareSettingsError.c_str());
    if (ImGui::Button("恢复上次有效配置"))
    {
        if (HardwareSettingsService::RestoreLastValid({}, &hardwareSettingsError))
        {
            hardwareUiInitialized = false;
            LogSystem::Add(LOG_WARN,
                "event=config_restored subsystem=hardware path=%s",
                HardwareSettingsService::SettingsPath().c_str());
        }
        else
        {
            LogSystem::Add(LOG_ERROR,
                "event=config_restore_failed subsystem=hardware error=%s",
                hardwareSettingsError.c_str());
        }
    }

}
}
