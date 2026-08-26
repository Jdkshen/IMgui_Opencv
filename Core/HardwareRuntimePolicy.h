#pragma once

#include "HardwareRuntimeService.h"

namespace HardwareRuntimePolicy
{
    DeviceOperationResult ValidateHandshakeConfig(
        const HardwareHandshakeConfig& config);
    std::vector<std::string> ExtractQrSerials(
        const std::vector<ToolResult>& results);
    std::string BuildQrJsonPayload(ToolResultStatus status,
        const std::vector<std::string>& serials);
}
