#include "HardwareWindow.h"

#include "DockSpaceHost.h"
#include "../Core/FrameArchiveService.h"
#include "../Core/HardwareRuntimeService.h"
#include "../Core/HardwareSettingsService.h"
#include "../Core/OpenFileDialog.h"
#include "../Core/ThemeManager.h"
#include "../Log/LogSystem.h"
#include "../include/imgui/imgui.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <cstring>
#include <string>

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
    static int cameraTimeoutMs = 250;
    static int cameraIntervalMs = 33;
    static bool cameraAutoCapture = true;
    static bool cameraRunAfterCapture = true;
    static bool cameraTriggerBeforeRun = true;
    static bool cameraAutoExposure = true;
    static float cameraExposure = -6.0f;
    static float cameraGain = 0.0f;
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

    if (!hardwareUiInitialized)
    {
        const HardwarePanelSettings settings = HardwareSettingsService::Load();
        std::snprintf(cameraAddress, sizeof(cameraAddress), "%s", settings.cameraAddress.c_str());
        std::snprintf(cameraSourceName, sizeof(cameraSourceName), "%s", settings.cameraSourceName.c_str());
        cameraBackend = settings.cameraBackend;
        cameraTimeoutMs = settings.cameraTimeoutMs;
        cameraIntervalMs = settings.cameraIntervalMs;
        cameraAutoCapture = settings.cameraAutoCapture;
        cameraRunAfterCapture = settings.cameraRunAfterCapture;
        cameraTriggerBeforeRun = settings.cameraTriggerBeforeRun;
        cameraAutoExposure = settings.cameraAutoExposure;
        cameraExposure = settings.cameraExposure;
        cameraGain = settings.cameraGain;

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

    DrawSectionTitle("工业相机");
    ImGui::TextColored(ConnectionStateColor(snapshot.cameraState), "%s%s%s",
        ConnectionStateName(snapshot.cameraState),
        snapshot.cameraAdapterName.empty() ? "" : " · ",
        snapshot.cameraAdapterName.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("帧 %d%s", snapshot.cameraFrameIndex,
        snapshot.cameraCapturePending ? " · 抓取中" : "");

    const char* cameraBackends[] = {"自动", "DirectShow", "Media Foundation", "FFmpeg", "GStreamer"};
    if (BeginPropertyTable("##camera_properties"))
    {
        PropertyRow("相机地址");
        hardwareSettingsChanged |= ImGui::InputText("##camera_address", cameraAddress, sizeof(cameraAddress));
        ImGui::SetItemTooltip("相机索引（例如 0）或 RTSP/HTTP 视频流 URL");

        PropertyRow("采集后端");
        hardwareSettingsChanged |= ImGui::Combo("##camera_backend", &cameraBackend, cameraBackends,
            static_cast<int>(std::size(cameraBackends)));

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
        config.autoCapture = cameraAutoCapture;
        config.triggerOnInspection = cameraTriggerBeforeRun;
        config.autoExposure = cameraAutoExposure;
        config.exposure = cameraExposure;
        config.gain = cameraGain;
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
        if (ImGui::Checkbox("批次完成后发布##output_auto_publish", &outputAutoPublish))
        {
            hardwareSettingsChanged = true;
            if (snapshot.outputState == DeviceConnectionState::Connected)
                HardwareRuntimeService::SetOutputAutoPublish(outputAutoPublish);
        }
        ImGui::EndTable();
    }

    const bool outputConnected = snapshot.outputState == DeviceConnectionState::Connected;
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
        LogOperation("硬件输出连接", HardwareRuntimeService::ConnectOutput(config));
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
        settings.cameraTimeoutMs = cameraTimeoutMs;
        settings.cameraIntervalMs = cameraIntervalMs;
        settings.cameraAutoCapture = cameraAutoCapture;
        settings.cameraRunAfterCapture = cameraRunAfterCapture;
        settings.cameraTriggerBeforeRun = cameraTriggerBeforeRun;
        settings.cameraAutoExposure = cameraAutoExposure;
        settings.cameraExposure = cameraExposure;
        settings.cameraGain = cameraGain;
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
