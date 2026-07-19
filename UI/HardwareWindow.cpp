#include "HardwareWindow.h"

#include "DockSpaceHost.h"
#include "../Core/HardwareRuntimeService.h"
#include "../Core/ThemeManager.h"
#include "../Log/LogSystem.h"
#include "../include/imgui/imgui.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iterator>
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

    HardwareRuntimeSnapshot snapshot = HardwareRuntimeService::Snapshot();

    DrawSectionTitle("工业相机");
    ImGui::TextColored(ConnectionStateColor(snapshot.cameraState), "%s%s%s",
        ConnectionStateName(snapshot.cameraState),
        snapshot.cameraAdapterName.empty() ? "" : " · ",
        snapshot.cameraAdapterName.c_str());
    if (snapshot.cameraCapturePending)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("抓帧中");
    }
    ImGui::TextDisabled("已发布帧: %d", snapshot.cameraFrameIndex);

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##camera_address", cameraAddress, sizeof(cameraAddress));
    ImGui::SetItemTooltip("相机索引，例如 0；也可填写 RTSP/HTTP/视频流 URL");

    const char* cameraBackends[] = {"自动", "DirectShow", "Media Foundation", "FFmpeg", "GStreamer"};
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::Combo("##camera_backend", &cameraBackend, cameraBackends,
        static_cast<int>(std::size(cameraBackends)));
    ImGui::SetItemTooltip("OpenCV VideoCapture 后端");

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##camera_source_name", cameraSourceName, sizeof(cameraSourceName));
    ImGui::SetItemTooltip("写入 FrameSourceState 的来源名称");
    ImGui::TextDisabled("抓帧超时 (ms)");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputInt("##camera_timeout", &cameraTimeoutMs);
    ImGui::TextDisabled("抓帧间隔 (ms)");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputInt("##camera_interval", &cameraIntervalMs);
    if (ImGui::Checkbox("自动抓帧", &cameraAutoCapture))
        HardwareRuntimeService::SetCameraAutoCapture(cameraAutoCapture);
    ImGui::SameLine();
    ImGui::Checkbox("抓帧后执行", &cameraRunAfterCapture);

    const float twoButtonWidth = (ImGui::GetContentRegionAvail().x -
        ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("连接相机", ImVec2(twoButtonWidth, 0.0f)))
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
        LogOperation("工业相机连接", HardwareRuntimeService::ConnectCamera(config));
    }
    ImGui::SameLine();
    if (ImGui::Button(cameraRunAfterCapture ? "抓帧并执行" : "抓取一帧",
        ImVec2(twoButtonWidth, 0.0f)))
    {
        HardwareRuntimeService::RequestCameraFrame(cameraRunAfterCapture);
    }
    if (ImGui::Button("断开相机", ImVec2(-1.0f, 0.0f)))
        HardwareRuntimeService::DisconnectCamera();
    DrawOperationMessage(snapshot.lastCameraOperation);

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
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::Combo("##output_type", &outputType, outputTypes,
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

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##output_key", outputKey, sizeof(outputKey));
    ImGui::SetItemTooltip("Core 设备注册标识");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##output_address", outputAddress, sizeof(outputAddress));
    ImGui::SetItemTooltip(outputType == 2
        ? "OPC UA 主机或完整 opc.tcp:// URL"
        : outputType == 3 ? "普通 TCP Server 主机或 IP" : "PLC/Modbus TCP 主机或 IP");
    ImGui::TextDisabled("端口");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputInt("##output_port", &outputPort);
    if (outputType != 3)
    {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##output_resource", outputResource, sizeof(outputResource));
        ImGui::SetItemTooltip(outputType == 2 ? "可选 OPC UA endpoint path" :
            "Modbus Unit ID，通常为 1；直连设备也可能使用 0 或 255");
    }
    ImGui::TextDisabled("连接超时 (ms)");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputInt("##output_timeout", &outputTimeoutMs);

    if (outputType == 1 || outputType == 2)
    {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##output_target", outputTarget, sizeof(outputTarget));
        ImGui::SetItemTooltip(outputType == 1 ? "PLC 标签名" : "例如 ns=2;s=Inspection.OK");
    }
    if (outputType == 0 || outputType == 1)
    {
        ImGui::TextDisabled("%s", outputType == 0 ? "线圈地址" : "映射地址");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputInt("##output_mapping_address", &outputAddressValue);
        ImGui::SetItemTooltip("Modbus 协议地址从 0 开始；PLC 显示 00001 时通常填写 0");
        if (outputType == 1)
            ImGui::Checkbox("映射到保持寄存器", &plcHoldingRegister);
    }
    if (outputType == 3)
    {
        const float textWidth = (ImGui::GetContentRegionAvail().x -
            ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        ImGui::SetNextItemWidth(textWidth);
        ImGui::InputText("##tcp_pass_text", tcpPassText, sizeof(tcpPassText));
        ImGui::SetItemTooltip("Pass 输出内容");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(textWidth);
        ImGui::InputText("##tcp_fail_text", tcpFailText, sizeof(tcpFailText));
        ImGui::SetItemTooltip("Fail/Error 输出内容");
        ImGui::Checkbox("追加 CRLF", &tcpAppendCrLf);
        ImGui::SetItemTooltip("发送文本后不等待服务端响应");
        ImGui::SameLine();
        ImGui::Checkbox("输出反相", &outputInvert);
    }
    else
    {
        ImGui::Checkbox("输出反相", &outputInvert);
    }
    if (ImGui::Checkbox("批次完成自动发布", &outputAutoPublish) &&
        snapshot.outputState == DeviceConnectionState::Connected)
    {
        HardwareRuntimeService::SetOutputAutoPublish(outputAutoPublish);
    }

    if (ImGui::Button("连接输出", ImVec2(twoButtonWidth, 0.0f)))
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
    ImGui::SameLine();
    if (ImGui::Button("断开输出", ImVec2(twoButtonWidth, 0.0f)))
        HardwareRuntimeService::DisconnectOutput();

    const bool outputConnected = snapshot.outputState == DeviceConnectionState::Connected;
    ImGui::BeginDisabled(!outputConnected);
    if (ImGui::Button("测试 Pass", ImVec2(twoButtonWidth, 0.0f)))
        LogOperation("测试 Pass 输出", HardwareRuntimeService::PublishConfiguredStatus(ToolResultStatus::Pass));
    ImGui::SameLine();
    if (ImGui::Button("测试 Fail", ImVec2(twoButtonWidth, 0.0f)))
        LogOperation("测试 Fail 输出", HardwareRuntimeService::PublishConfiguredStatus(ToolResultStatus::Fail));
    ImGui::EndDisabled();
    DrawOperationMessage(snapshot.lastOutputOperation);

}
}
