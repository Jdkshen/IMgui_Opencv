#pragma once

#include "HardwareRuntimeService.h"

#include <string>
#include <vector>

namespace HardwareHandshakePlan
{
    struct IoWrite
    {
        HardwareIoMapping mapping;
        bool active = false;
        bool usePulse = false;
    };

    std::vector<IoWrite> BuildSignal(const HardwareHandshakeConfig& config,
        HardwareIoSignal signal, bool active, bool usePulse = true,
        const std::string& taskGroupName = {});
    std::vector<IoWrite> BuildStart(const HardwareHandshakeConfig& config,
        const std::string& taskGroupName, bool testActive);
    std::vector<IoWrite> BuildComplete(const HardwareHandshakeConfig& config,
        ToolResultStatus status, const std::string& taskGroupName, bool testActive);
    std::vector<IoWrite> BuildReset(const HardwareHandshakeConfig& config,
        const std::string& taskGroupName, bool testActive);
    bool HasAcknowledgeInput(const HardwareHandshakeConfig& config);
}
