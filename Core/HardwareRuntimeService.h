#pragma once

#include "HardwareAdapters.h"
#include "../Algorithm/ToolResult.h"

#include <cstdint>
#include <string>

enum class HardwareOutputKind
{
    PlcTag,
    ModbusCoil,
    OpcUaNode
};

struct HardwareOutputBinding
{
    HardwareOutputKind kind = HardwareOutputKind::PlcTag;
    std::string adapterKey;
    std::string target;
    std::uint16_t address = 0;
    bool invert = false;
};

namespace HardwareRuntimeService
{
    DeviceOperationResult GrabCameraFrame(int timeoutMs = 1000,
        const std::string& sourceName = "camera", int frameIndex = -1,
        double timestampMs = 0.0);

    DeviceOperationResult PublishInspectionStatus(ToolResultStatus status,
        const HardwareOutputBinding& binding);
}
