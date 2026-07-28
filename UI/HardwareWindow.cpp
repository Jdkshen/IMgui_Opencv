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
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <cstring>
#include <string>
#include <vector>

namespace
{
bool s_focusHardwareWindow = false;

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

}

namespace UI
{
void RequestHardwareWindowFocus()
{
    s_focusHardwareWindow = true;
}

void ShowHardwareWindow()
{
    if (!g_ShowHardware)
        return;

    if (s_focusHardwareWindow)
        ImGui::SetNextWindowFocus();

    if (ImGui::Begin("设备连接", &g_ShowHardware))
        DrawHardwarePanel();
    ImGui::End();

    s_focusHardwareWindow = false;
}

void DrawHardwarePanel()
{
    static char cameraAddress[256] = "0";
    static char cameraSourceName[96] = "industrial-camera";
    static int cameraBackend = 0;
    static int cameraOrientation = 0;
    static int cameraTimeoutMs = 250;
    static int cameraIntervalMs = 33;
    static bool cameraAutoCapture = true;
    static bool cameraRunAfterCapture = true;
    static bool cameraTriggerBeforeRun = true;
    static bool cameraAutoExposure = true;
    static float cameraExposure = -6.0f;
    static float cameraGain = 0.0f;
    static bool cameraAutoReconnect = true;
    static int cameraReconnectFailureThreshold = 3;
    static int cameraReconnectInitialDelayMs = 250;
    static int cameraReconnectMaxDelayMs = 5000;
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
    static std::string manualTriggerTask;
    static std::string scrollToTriggerTask;
    static std::string triggerSyncMessage;
    static std::vector<HardwareTaskIdentity> synchronizedTaskGroups;
    static bool outputConfigurationDirty = false;

    if (!hardwareUiInitialized)
    {
        const HardwarePanelSettings settings = HardwareSettingsService::Load();
        std::snprintf(cameraAddress, sizeof(cameraAddress), "%s", settings.cameraAddress.c_str());
        std::snprintf(cameraSourceName, sizeof(cameraSourceName), "%s", settings.cameraSourceName.c_str());
        cameraBackend = settings.cameraBackend;
        cameraOrientation = settings.cameraOrientation;
        cameraTimeoutMs = settings.cameraTimeoutMs;
        cameraIntervalMs = settings.cameraIntervalMs;
        cameraAutoCapture = settings.cameraAutoCapture;
        cameraRunAfterCapture = settings.cameraRunAfterCapture;
        cameraTriggerBeforeRun = settings.cameraTriggerBeforeRun;
        cameraAutoExposure = settings.cameraAutoExposure;
        cameraExposure = settings.cameraExposure;
        cameraGain = settings.cameraGain;
        cameraAutoReconnect = settings.cameraAutoReconnect;
        cameraReconnectFailureThreshold = settings.cameraReconnectFailureThreshold;
        cameraReconnectInitialDelayMs = settings.cameraReconnectInitialDelayMs;
        cameraReconnectMaxDelayMs = settings.cameraReconnectMaxDelayMs;

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
    bool hardwareSettingsChanged = false;

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

    DrawSectionTitle("工业相机");
    ImGui::TextColored(ConnectionStateColor(snapshot.cameraState), "%s%s%s",
        ConnectionStateName(snapshot.cameraState),
        snapshot.cameraAdapterName.empty() ? "" : " · ",
        snapshot.cameraAdapterName.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("帧 %d%s", snapshot.cameraFrameIndex,
        snapshot.cameraCapturePending ? " · 抓取中" : "");
    if (snapshot.cameraReconnecting || snapshot.cameraConsecutiveFailures > 0)
    {
        ImGui::TextDisabled("重连 %d 次 · 连续失败 %d · 退避 %d ms%s",
            snapshot.cameraReconnectAttempts, snapshot.cameraConsecutiveFailures,
            snapshot.cameraReconnectDelayMs,
            snapshot.cameraReconnecting ? " · 重连中" : "");
    }

    const char* cameraBackends[] = {"自动", "DirectShow", "Media Foundation", "FFmpeg", "GStreamer"};
    if (BeginPropertyTable("##camera_properties"))
    {
        PropertyRow("相机地址");
        hardwareSettingsChanged |= ImGui::InputText("##camera_address", cameraAddress, sizeof(cameraAddress));
        ImGui::SetItemTooltip("相机索引（例如 0）或 RTSP/HTTP 视频流 URL");

        PropertyRow("采集后端");
        hardwareSettingsChanged |= ImGui::Combo("##camera_backend", &cameraBackend, cameraBackends,
            static_cast<int>(std::size(cameraBackends)));

        PropertyRow("图像方向");
        const char* cameraOrientations[] = {
            "原始方向", "顺时针 90°", "旋转 180°", "逆时针 90°", "水平镜像", "垂直镜像"};
        if (ImGui::Combo("##camera_orientation", &cameraOrientation,
            cameraOrientations, static_cast<int>(std::size(cameraOrientations))))
        {
            hardwareSettingsChanged = true;
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
            HardwareRuntimeService::SetCameraControl(
                CameraControl::AutoExposure, cameraAutoExposure ? 1.0 : 0.0);
        }

        PropertyRow("曝光值");
        ImGui::BeginDisabled(cameraAutoExposure);
        if (ImGui::DragFloat("##camera_exposure", &cameraExposure, 0.1f, -13.0f, 5.0f, "%.2f"))
        {
            hardwareSettingsChanged = true;
            HardwareRuntimeService::SetCameraControl(CameraControl::Exposure, cameraExposure);
        }
        ImGui::EndDisabled();

        PropertyRow("增益");
        if (ImGui::DragFloat("##camera_gain", &cameraGain, 0.5f, 0.0f, 100.0f, "%.1f"))
        {
            hardwareSettingsChanged = true;
            HardwareRuntimeService::SetCameraControl(CameraControl::Gain, cameraGain);
        }

        PropertyRow("采集模式");
        if (ImGui::Checkbox("自动抓帧##camera_auto_capture", &cameraAutoCapture))
        {
            hardwareSettingsChanged = true;
            HardwareRuntimeService::SetCameraAutoCapture(cameraAutoCapture);
        }

        PropertyRow("执行联动");
        hardwareSettingsChanged |= ImGui::Checkbox("抓帧后执行##camera_run_after", &cameraRunAfterCapture);
        if (!IsNarrowPanel())
            ImGui::SameLine();
        if (ImGui::Checkbox("执行前触发##camera_trigger_before", &cameraTriggerBeforeRun))
        {
            hardwareSettingsChanged = true;
            HardwareRuntimeService::SetCameraTriggerOnInspection(cameraTriggerBeforeRun);
        }

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

    const bool narrowPanel = IsNarrowPanel();
    const float twoButtonWidth = TwoColumnButtonWidth();
    const float cameraButtonWidth = narrowPanel ? -1.0f : twoButtonWidth;
    const bool cameraConnected = snapshot.cameraState == DeviceConnectionState::Connected;
    if (ImGui::Button(cameraConnected ? "重新连接" : "连接相机",
        ImVec2(cameraButtonWidth, kActionButtonHeight)))
    {
        static const char* backendValues[] = {"", "dshow", "msmf", "ffmpeg", "gstreamer"};
        HardwareCameraConnectionConfig config;
        config.endpoint.address = cameraAddress;
        config.endpoint.resource = backendValues[std::clamp(cameraBackend, 0, 4)];
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
        config.autoReconnect = cameraAutoReconnect;
        config.reconnectFailureThreshold = cameraReconnectFailureThreshold;
        config.reconnectInitialDelayMs = cameraReconnectInitialDelayMs;
        config.reconnectMaxDelayMs = cameraReconnectMaxDelayMs;
        LogOperation("工业相机连接", HardwareRuntimeService::ConnectCamera(config));
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
        HardwareRuntimeService::DisconnectCamera();
    ImGui::EndDisabled();
    DrawOperationMessage(snapshot.lastCameraOperation);

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

    DrawSectionTitle("检测结果输出");
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
        "Modbus TCP 线圈", "Modbus PLC 标签", "OPC UA NodeId", "TCP 文本"};
    const int previousOutputType = outputType;
    if (BeginPropertyTable("##output_properties"))
    {
        PropertyRow("输出类型");
        hardwareSettingsChanged |= ImGui::Combo("##output_type", &outputType, outputTypes,
            static_cast<int>(std::size(outputTypes)));
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
            PropertyRow("Pass 文本");
            hardwareSettingsChanged |= ImGui::InputText("##tcp_pass_text", tcpPassText, sizeof(tcpPassText));
            PropertyRow("Fail 文本");
            hardwareSettingsChanged |= ImGui::InputText("##tcp_fail_text", tcpFailText, sizeof(tcpFailText));
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

    const bool outputConnected = snapshot.outputState == DeviceConnectionState::Connected;
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
        int removeMapping = -1;
        if (ImGui::BeginTable("##plc_io_mapping", 9,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollX | ImGuiTableFlags_SizingFixedFit,
            ImVec2(0.0f, (std::min)(280.0f,
                58.0f + outputIoMappings.size() * 30.0f))))
        {
            ImGui::TableSetupColumn("启用", ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn("信号", ImGuiTableColumnFlags_WidthFixed, 118.0f);
            ImGui::TableSetupColumn("方向", ImGuiTableColumnFlags_WidthFixed, 98.0f);
            ImGui::TableSetupColumn("地址", ImGuiTableColumnFlags_WidthFixed, 72.0f);
            ImGui::TableSetupColumn("反相", ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn("脉冲", ImGuiTableColumnFlags_WidthFixed, 82.0f);
            ImGui::TableSetupColumn("任务", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("单点测试", ImGuiTableColumnFlags_WidthFixed, 104.0f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 34.0f);
            ImGui::TableHeadersRow();

            const char* signalItems[] = {
                "触发 Trigger", "运行 Busy", "完成 Done", "合格 OK",
                "不合格 NG", "异常 Error", "心跳 Heartbeat", "确认 ACK"};
            const char* directionItems[] = {"PLC → 视觉", "视觉 → PLC"};
            for (std::size_t index = 0; index < outputIoMappings.size(); ++index)
            {
                HardwareIoMapping& mapping = outputIoMappings[index];
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
                if (mapping.signal == HardwareIoSignal::Trigger)
                {
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::BeginCombo("##task", mapping.taskGroupName.empty()
                        ? "选择任务" : mapping.taskGroupName.c_str()))
                    {
                        for (const TaskGroupDefinition& group :
                            ToolChainState::ReadOnlyTaskGroups())
                        {
                            if (ImGui::Selectable(group.name.c_str(),
                                mapping.taskGroupName == group.name))
                            {
                                mapping.taskGroupName = group.name;
                                handshakeSettingsChanged = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
                else
                {
                    ImGui::TextDisabled("—");
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
                if (!scrollToTriggerTask.empty() &&
                    mapping.signal == HardwareIoSignal::Trigger &&
                    mapping.taskGroupName == scrollToTriggerTask)
                {
                    ImGui::SetScrollHereY(0.25f);
                    scrollToTriggerTask.clear();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
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
            if (!ToolChainState::ReadOnlyTaskGroups().empty())
                mapping.taskGroupName = ToolChainState::ReadOnlyTaskGroups().front().name;
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
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::BeginCombo("##manual_plc_task", manualTriggerTask.empty()
            ? "选择测试任务" : manualTriggerTask.c_str()))
        {
            for (const TaskGroupDefinition& group : ToolChainState::ReadOnlyTaskGroups())
            {
                if (ImGui::Selectable(group.name.c_str(),
                    manualTriggerTask == group.name))
                {
                    manualTriggerTask = group.name;
                    scrollToTriggerTask = group.name;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
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

    if (hardwareSettingsChanged)
    {
        HardwarePanelSettings settings;
        settings.cameraAddress = cameraAddress;
        settings.cameraSourceName = cameraSourceName;
        settings.cameraBackend = cameraBackend;
        settings.cameraOrientation = cameraOrientation;
        settings.cameraTimeoutMs = cameraTimeoutMs;
        settings.cameraIntervalMs = cameraIntervalMs;
        settings.cameraAutoCapture = cameraAutoCapture;
        settings.cameraRunAfterCapture = cameraRunAfterCapture;
        settings.cameraTriggerBeforeRun = cameraTriggerBeforeRun;
        settings.cameraAutoExposure = cameraAutoExposure;
        settings.cameraExposure = cameraExposure;
        settings.cameraGain = cameraGain;
        settings.cameraAutoReconnect = cameraAutoReconnect;
        settings.cameraReconnectFailureThreshold = cameraReconnectFailureThreshold;
        settings.cameraReconnectInitialDelayMs = cameraReconnectInitialDelayMs;
        settings.cameraReconnectMaxDelayMs = cameraReconnectMaxDelayMs;
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
        HardwareSettingsService::Save(settings, {}, &hardwareSettingsError);
    }

    ImGui::Spacing();
    ImGui::TextDisabled("设备参数：修改后自动保存");
    ImGui::TextWrapped("%s", HardwareSettingsService::SettingsPath().c_str());
    if (!hardwareSettingsError.empty())
        ImGui::TextColored(ImVec4(0.95f, 0.38f, 0.32f, 1.0f),
            "保存失败：%s", hardwareSettingsError.c_str());

}
}
