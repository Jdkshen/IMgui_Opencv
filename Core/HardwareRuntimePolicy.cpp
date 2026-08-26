#include "HardwareRuntimePolicy.h"

#include <nlohmann/json.hpp>

#include <set>

namespace HardwareRuntimePolicy
{
DeviceOperationResult ValidateHandshakeConfig(
    const HardwareHandshakeConfig& config)
{
    if (!config.enabled)
        return {true, {}};
    bool hasTrigger = false;
    bool hasBusy = false;
    bool hasDone = false;
    bool hasResult = false;
    std::set<std::uint16_t> addresses;
    for (const HardwareIoMapping& mapping : config.mappings)
    {
        if (!mapping.enabled)
            continue;
        if (!addresses.insert(mapping.address).second)
            return {false, "PLC IO 地址重复: " + std::to_string(mapping.address)};
        const bool inputSignal = mapping.signal == HardwareIoSignal::Trigger ||
            mapping.signal == HardwareIoSignal::Acknowledge;
        const HardwareIoDirection expectedDirection = inputSignal
            ? HardwareIoDirection::Input : HardwareIoDirection::Output;
        if (mapping.direction != expectedDirection)
        {
            return {false, "PLC IO 信号方向不正确，地址: " +
                std::to_string(mapping.address)};
        }
        if (mapping.signal == HardwareIoSignal::Trigger)
        {
            if (mapping.taskGroupName.empty())
                return {false, "Trigger 必须绑定任务"};
            hasTrigger = true;
        }
        hasBusy |= mapping.signal == HardwareIoSignal::Busy;
        hasDone |= mapping.signal == HardwareIoSignal::Done;
        hasResult |= mapping.signal == HardwareIoSignal::Ok ||
            mapping.signal == HardwareIoSignal::Ng ||
            mapping.signal == HardwareIoSignal::Error;
    }
    if (!hasTrigger || !hasBusy || !hasDone || !hasResult)
    {
        return {false,
            "PLC 握手至少需要 Trigger、Busy、Done 和一个结果输出"};
    }
    return {true, {}};
}

std::vector<std::string> ExtractQrSerials(
    const std::vector<ToolResult>& results)
{
    std::vector<std::string> serials;
    for (const ToolResult& result : results)
    {
        if (result.toolName.find("二维码") == std::string::npos &&
            result.toolName.find("条码") == std::string::npos)
        {
            continue;
        }
        for (const ToolResult::TextItem& text : result.texts)
        {
            if (!text.text.empty())
                serials.push_back(text.text);
        }
    }
    return serials;
}

std::string BuildQrJsonPayload(ToolResultStatus status,
    const std::vector<std::string>& serials)
{
    nlohmann::json payload;
    payload["result"] = status == ToolResultStatus::Pass ? "OK" :
        status == ToolResultStatus::Fail ? "NG" : "ERROR";
    payload["serial"] = serials.empty() ? "" : serials.front();
    payload["serials"] = serials;
    return payload.dump();
}
}

namespace HardwareRuntimeService
{
ToolResultStatus AggregateInspectionStatus(const std::vector<ToolResult>& results)
{
    bool hasResult = false;
    ToolResultStatus aggregate = ToolResultStatus::Pass;
    for (const ToolResult& result : results)
    {
        if (result.skipped)
            continue;
        hasResult = true;
        if (result.status == ToolResultStatus::Error)
            return ToolResultStatus::Error;
        if (result.status == ToolResultStatus::Fail)
            aggregate = ToolResultStatus::Fail;
    }
    return hasResult ? aggregate : ToolResultStatus::Error;
}
}
