#include "HardwareRuntimeService.h"

#include "FrameSourceState.h"

#include <opencv2/core/mat.hpp>

#include <algorithm>

namespace
{
DeviceOperationResult NotConnected(const char* name)
{
    return {false, std::string(name) + " 未连接"};
}
}

namespace HardwareRuntimeService
{
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

    FrameSourceState::SetCurrentFrame(frame, FrameSourceType::Camera,
        sourceName.empty() ? camera->AdapterName() : sourceName,
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
    }

    return {false, "不支持的硬件输出类型"};
}
}
