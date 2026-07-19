#include "HardwareWindow.h"

#include "DockSpaceHost.h"
#include "../Core/HardwareRuntimeService.h"
#include "../Log/LogSystem.h"
#include "../include/imgui/imgui.h"

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>

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
    ImGui::TextColored(result.success
        ? ImVec4(0.35f, 0.78f, 0.48f, 1.0f)
        : ImVec4(0.95f, 0.38f, 0.32f, 1.0f),
        "%s", result.message.c_str());
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
void ShowHardwareWindow()
{
    if (!g_ShowHardware)
        return;

    static char cameraAddress[256] = "0";
    static char cameraSourceName[96] = "industrial-camera";
    static int cameraBackend = 0;
    static int cameraTimeoutMs = 250;
    static int cameraIntervalMs = 33;
    static bool cameraAutoCapture = true;

    static int outputType = 0;
    static char outputKey[96] = "output-main";
    static char outputAddress[256] = "127.0.0.1";
    static int outputPort = 502;
    static char outputResource[128] = "1";
    static char outputTarget[256] = "ns=2;s=Inspection.OK";
    static int outputAddressValue = 0;
    static int outputTimeoutMs = 1500;
    static bool plcHoldingRegister = false;
    static bool outputInvert = false;
    static bool outputAutoPublish = false;

    HardwareRuntimeSnapshot snapshot = HardwareRuntimeService::Snapshot();

    ImGui::SetNextWindowSizeConstraints(ImVec2(420.0f, 360.0f), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::Begin("设备连接", &g_ShowHardware);

    ImGui::TextUnformatted("工业相机");
    ImGui::Separator();
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
    ImGui::InputInt("抓帧超时(ms)", &cameraTimeoutMs);
    ImGui::InputInt("抓帧间隔(ms)", &cameraIntervalMs);
    if (ImGui::Checkbox("自动抓帧", &cameraAutoCapture))
        HardwareRuntimeService::SetCameraAutoCapture(cameraAutoCapture);

    if (ImGui::Button("连接相机"))
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
    if (ImGui::Button("抓取一帧"))
        HardwareRuntimeService::RequestCameraFrame();
    ImGui::SameLine();
    if (ImGui::Button("断开相机"))
        HardwareRuntimeService::DisconnectCamera();
    DrawOperationMessage(snapshot.lastCameraOperation);

    ImGui::Spacing();
    ImGui::TextUnformatted("检测结果输出");
    ImGui::Separator();
    ImGui::TextColored(ConnectionStateColor(snapshot.outputState), "%s%s%s",
        ConnectionStateName(snapshot.outputState),
        snapshot.outputAdapterName.empty() ? "" : " · ",
        snapshot.outputAdapterName.c_str());
    if (!snapshot.outputAdapterKey.empty())
        ImGui::TextDisabled("适配器: %s", snapshot.outputAdapterKey.c_str());

    const char* outputTypes[] = {"Modbus TCP 线圈", "Modbus PLC 标签", "OPC UA NodeId"};
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
        std::snprintf(outputResource, sizeof(outputResource), "%s", outputType == 2 ? "" : "1");
    }

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##output_key", outputKey, sizeof(outputKey));
    ImGui::SetItemTooltip("Core 设备注册标识");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##output_address", outputAddress, sizeof(outputAddress));
    ImGui::SetItemTooltip(outputType == 2
        ? "OPC UA 主机或完整 opc.tcp:// URL"
        : "PLC/Modbus TCP 主机或 IP");
    ImGui::InputInt("端口", &outputPort);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##output_resource", outputResource, sizeof(outputResource));
    ImGui::SetItemTooltip(outputType == 2 ? "可选 OPC UA endpoint path" :
        "Modbus Unit ID，通常为 1；直连设备也可能使用 0 或 255");
    ImGui::InputInt("连接超时(ms)", &outputTimeoutMs);

    if (outputType == 1 || outputType == 2)
    {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##output_target", outputTarget, sizeof(outputTarget));
        ImGui::SetItemTooltip(outputType == 1 ? "PLC 标签名" : "例如 ns=2;s=Inspection.OK");
    }
    if (outputType != 2)
    {
        ImGui::InputInt(outputType == 0 ? "线圈地址" : "映射地址", &outputAddressValue);
        ImGui::SetItemTooltip("Modbus 协议地址从 0 开始；PLC 显示 00001 时通常填写 0");
        if (outputType == 1)
            ImGui::Checkbox("映射到保持寄存器", &plcHoldingRegister);
    }
    ImGui::Checkbox("输出反相", &outputInvert);
    if (ImGui::Checkbox("批次完成自动发布", &outputAutoPublish) &&
        snapshot.outputState == DeviceConnectionState::Connected)
    {
        HardwareRuntimeService::SetOutputAutoPublish(outputAutoPublish);
    }

    if (ImGui::Button("连接输出设备"))
    {
        HardwareOutputConnectionConfig config;
        config.adapterType = static_cast<HardwareOutputAdapterType>(std::clamp(outputType, 0, 2));
        config.endpoint.address = outputAddress;
        config.endpoint.port = ClampPort(outputPort);
        config.endpoint.resource = outputResource;
        config.endpoint.timeoutMs = std::max(1, outputTimeoutMs);
        config.binding.adapterKey = outputKey;
        config.binding.target = outputTarget;
        config.binding.address = ClampAddress(outputAddressValue);
        config.binding.invert = outputInvert;
        config.plcUseHoldingRegister = plcHoldingRegister;
        config.autoPublish = outputAutoPublish;
        LogOperation("硬件输出连接", HardwareRuntimeService::ConnectOutput(config));
    }
    ImGui::SameLine();
    if (ImGui::Button("断开输出"))
        HardwareRuntimeService::DisconnectOutput();

    const bool outputConnected = snapshot.outputState == DeviceConnectionState::Connected;
    ImGui::BeginDisabled(!outputConnected);
    if (ImGui::Button("测试 Pass"))
        LogOperation("测试 Pass 输出", HardwareRuntimeService::PublishConfiguredStatus(ToolResultStatus::Pass));
    ImGui::SameLine();
    if (ImGui::Button("测试 Fail"))
        LogOperation("测试 Fail 输出", HardwareRuntimeService::PublishConfiguredStatus(ToolResultStatus::Fail));
    ImGui::EndDisabled();
    DrawOperationMessage(snapshot.lastOutputOperation);

    ImGui::End();
}
}
