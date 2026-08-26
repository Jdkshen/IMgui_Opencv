#include "HardwareHandshakePlan.h"

#include <algorithm>

namespace HardwareHandshakePlan
{
namespace
{
void Append(std::vector<IoWrite>& target, std::vector<IoWrite> source)
{
    target.insert(target.end(),
        std::make_move_iterator(source.begin()),
        std::make_move_iterator(source.end()));
}

std::string ResultTaskName(const std::string& taskGroupName, bool testActive)
{
    return testActive ? std::string{} : taskGroupName;
}
}

std::vector<IoWrite> BuildSignal(const HardwareHandshakeConfig& config,
    HardwareIoSignal signal, bool active, bool usePulse,
    const std::string& taskGroupName)
{
    std::vector<IoWrite> writes;
    for (const HardwareIoMapping& mapping : config.mappings)
    {
        if (!mapping.enabled || mapping.direction != HardwareIoDirection::Output ||
            mapping.signal != signal)
        {
            continue;
        }
        if (!taskGroupName.empty() && !mapping.taskGroupName.empty() &&
            mapping.taskGroupName != taskGroupName)
        {
            continue;
        }
        writes.push_back({mapping, active, usePulse});
    }
    return writes;
}

std::vector<IoWrite> BuildStart(const HardwareHandshakeConfig& config,
    const std::string& taskGroupName, bool testActive)
{
    const std::string resultTask = ResultTaskName(taskGroupName, testActive);
    std::vector<IoWrite> writes;
    Append(writes, BuildSignal(config, HardwareIoSignal::Done, false, false));
    Append(writes, BuildSignal(config, HardwareIoSignal::Ok, false, false, resultTask));
    Append(writes, BuildSignal(config, HardwareIoSignal::Ng, false, false, resultTask));
    Append(writes, BuildSignal(config, HardwareIoSignal::Error, false, false));
    Append(writes, BuildSignal(config, HardwareIoSignal::Busy, true, false));
    return writes;
}

std::vector<IoWrite> BuildComplete(const HardwareHandshakeConfig& config,
    ToolResultStatus status, const std::string& taskGroupName, bool testActive)
{
    const std::string resultTask = ResultTaskName(taskGroupName, testActive);
    std::vector<IoWrite> writes;
    Append(writes, BuildSignal(config, HardwareIoSignal::Busy, false, false));
    Append(writes, BuildSignal(config, HardwareIoSignal::Ok,
        status == ToolResultStatus::Pass, false, resultTask));
    Append(writes, BuildSignal(config, HardwareIoSignal::Ng,
        status == ToolResultStatus::Fail, false, resultTask));
    Append(writes, BuildSignal(config, HardwareIoSignal::Error,
        status == ToolResultStatus::Error, false));
    Append(writes, BuildSignal(config, HardwareIoSignal::Done, true, true));
    return writes;
}

std::vector<IoWrite> BuildReset(const HardwareHandshakeConfig& config,
    const std::string& taskGroupName, bool testActive)
{
    const std::string resultTask = ResultTaskName(taskGroupName, testActive);
    std::vector<IoWrite> writes;
    Append(writes, BuildSignal(config, HardwareIoSignal::Busy, false, false));
    Append(writes, BuildSignal(config, HardwareIoSignal::Done, false, false));
    Append(writes, BuildSignal(config, HardwareIoSignal::Ok, false, false, resultTask));
    Append(writes, BuildSignal(config, HardwareIoSignal::Ng, false, false, resultTask));
    Append(writes, BuildSignal(config, HardwareIoSignal::Error, false, false));
    return writes;
}

bool HasAcknowledgeInput(const HardwareHandshakeConfig& config)
{
    return std::any_of(config.mappings.begin(), config.mappings.end(),
        [](const HardwareIoMapping& mapping)
        {
            return mapping.enabled &&
                mapping.direction == HardwareIoDirection::Input &&
                mapping.signal == HardwareIoSignal::Acknowledge;
        });
}
}
