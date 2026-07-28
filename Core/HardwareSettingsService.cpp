#include "HardwareSettingsService.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <utility>

namespace
{
namespace fs = std::filesystem;

fs::path AppDataDirectory()
{
    wchar_t* localAppData = nullptr;
    std::size_t length = 0;
    if (_wdupenv_s(&localAppData, &length, L"LOCALAPPDATA") == 0 &&
        localAppData && length > 1)
    {
        const fs::path path = fs::path(localAppData) / L"IMgui_Opencv";
        std::free(localAppData);
        return path;
    }
    std::free(localAppData);
    return fs::current_path();
}

fs::path Utf8Path(const std::string& value)
{
    const std::u8string utf8(reinterpret_cast<const char8_t*>(value.data()), value.size());
    return fs::path(utf8);
}

std::string PathToUtf8(const fs::path& value)
{
    const std::u8string utf8 = value.u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

fs::path DefaultSettingsFile()
{
    return AppDataDirectory() / L"hardware_settings.json";
}

fs::path ResolveSettingsFile(const std::string& path)
{
    return path.empty() ? DefaultSettingsFile() : Utf8Path(path);
}

HardwarePanelSettings Normalize(HardwarePanelSettings settings)
{
    settings.cameraBackend = std::clamp(settings.cameraBackend, 0, 4);
    settings.cameraOrientation = std::clamp(settings.cameraOrientation, 0, 5);
    settings.cameraTimeoutMs = std::clamp(settings.cameraTimeoutMs, 1, 10000);
    settings.cameraIntervalMs = std::clamp(settings.cameraIntervalMs, 1, 10000);
    settings.cameraExposure = std::clamp(settings.cameraExposure, -13.0f, 5.0f);
    settings.cameraGain = std::clamp(settings.cameraGain, 0.0f, 100.0f);
    settings.cameraReconnectFailureThreshold = std::clamp(
        settings.cameraReconnectFailureThreshold, 1, 100);
    settings.cameraReconnectInitialDelayMs = std::clamp(
        settings.cameraReconnectInitialDelayMs, 1, 60000);
    settings.cameraReconnectMaxDelayMs = std::clamp(
        settings.cameraReconnectMaxDelayMs,
        settings.cameraReconnectInitialDelayMs, 60000);
    settings.outputType = std::clamp(settings.outputType, 0, 3);
    settings.outputPort = std::clamp(settings.outputPort, 0, 65535);
    settings.outputAddressValue = std::clamp(settings.outputAddressValue, 0, 65535);
    settings.outputTimeoutMs = std::clamp(settings.outputTimeoutMs, 1, 60000);
    settings.outputQueueSize = std::clamp(settings.outputQueueSize, 1, 1024);
    settings.outputRetryCount = std::clamp(settings.outputRetryCount, 0, 10);
    settings.outputRetryDelayMs = std::clamp(settings.outputRetryDelayMs, 1, 60000);
    settings.outputPollIntervalMs = std::clamp(settings.outputPollIntervalMs, 10, 5000);
    settings.outputAcknowledgementTimeoutMs = std::clamp(
        settings.outputAcknowledgementTimeoutMs, 100, 60000);
    settings.outputInspectionTimeoutMs = std::clamp(
        settings.outputInspectionTimeoutMs, 1000, 600000);
    settings.outputHeartbeatIntervalMs = std::clamp(
        settings.outputHeartbeatIntervalMs, 100, 60000);
    settings.outputReconnectFailureThreshold = std::clamp(
        settings.outputReconnectFailureThreshold, 1, 100);
    settings.outputReconnectInitialDelayMs = std::clamp(
        settings.outputReconnectInitialDelayMs, 1, 60000);
    settings.outputReconnectMaxDelayMs = std::clamp(
        settings.outputReconnectMaxDelayMs,
        settings.outputReconnectInitialDelayMs, 60000);
    if (settings.outputIoMappings.size() > 64)
        settings.outputIoMappings.resize(64);
    for (HardwareIoMapping& mapping : settings.outputIoMappings)
        mapping.pulseMs = std::clamp(mapping.pulseMs, 0, 60000);
    return settings;
}

std::vector<std::string> NormalizeTaskGroupNames(
    const std::vector<std::string>& taskGroupNames)
{
    std::vector<std::string> normalized;
    std::set<std::string> seen;
    for (const std::string& name : taskGroupNames)
    {
        if (!name.empty() && seen.insert(name).second)
            normalized.push_back(name);
    }
    return normalized;
}

std::uint16_t DefaultTriggerAddress(std::size_t taskIndex)
{
    // 地址 1-7 留给 Busy/Done/OK/NG/Error/Heartbeat/ACK。
    return static_cast<std::uint16_t>(taskIndex == 0 ? 0 : taskIndex + 7);
}

std::uint16_t FindAvailableAddress(const std::set<std::uint16_t>& occupied,
    std::uint16_t preferred)
{
    for (std::uint32_t address = preferred; address <= 65535; ++address)
    {
        const auto candidate = static_cast<std::uint16_t>(address);
        if (!occupied.contains(candidate))
            return candidate;
    }
    for (std::uint32_t address = 0; address < preferred; ++address)
    {
        const auto candidate = static_cast<std::uint16_t>(address);
        if (!occupied.contains(candidate))
            return candidate;
    }
    return preferred;
}
}

namespace HardwareSettingsService
{
std::vector<HardwareIoMapping> BuildStandardIoMappings(
    const std::vector<std::string>& taskGroupNames)
{
    std::vector<std::string> names = NormalizeTaskGroupNames(taskGroupNames);
    if (names.empty())
        names.push_back("任务01");

    std::vector<HardwareIoMapping> mappings;
    mappings.reserve(names.size() + 7);
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        mappings.push_back({true, HardwareIoSignal::Trigger,
            HardwareIoDirection::Input, DefaultTriggerAddress(index),
            false, 0, names[index]});
    }

    const HardwarePanelSettings defaults;
    for (const HardwareIoMapping& mapping : defaults.outputIoMappings)
    {
        if (mapping.signal != HardwareIoSignal::Trigger)
            mappings.push_back(mapping);
    }
    return mappings;
}

bool EnsureTaskTriggerMappings(std::vector<HardwareIoMapping>& mappings,
    const std::vector<std::string>& taskGroupNames)
{
    const std::vector<std::string> names = NormalizeTaskGroupNames(taskGroupNames);
    if (names.empty())
        return false;

    std::set<std::string> boundTasks;
    std::set<std::uint16_t> occupiedAddresses;
    for (const HardwareIoMapping& mapping : mappings)
    {
        occupiedAddresses.insert(mapping.address);
        if (mapping.signal == HardwareIoSignal::Trigger &&
            !mapping.taskGroupName.empty())
        {
            boundTasks.insert(mapping.taskGroupName);
        }
    }

    std::vector<HardwareIoMapping> additions;
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        if (boundTasks.contains(names[index]))
            continue;
        const std::uint16_t address = FindAvailableAddress(
            occupiedAddresses, DefaultTriggerAddress(index));
        additions.push_back({true, HardwareIoSignal::Trigger,
            HardwareIoDirection::Input, address, false, 0, names[index]});
        occupiedAddresses.insert(address);
        boundTasks.insert(names[index]);
    }
    if (additions.empty())
        return false;

    const auto firstOutput = std::find_if(mappings.begin(), mappings.end(),
        [](const HardwareIoMapping& mapping)
        {
            return mapping.signal != HardwareIoSignal::Trigger;
        });
    mappings.insert(firstOutput, additions.begin(), additions.end());
    return true;
}

bool SynchronizeTaskTriggerMappings(std::vector<HardwareIoMapping>& mappings,
    const std::vector<HardwareTaskIdentity>& previousTaskGroups,
    const std::vector<HardwareTaskIdentity>& currentTaskGroups)
{
    bool changed = false;
    if (!previousTaskGroups.empty())
    {
        for (const HardwareTaskIdentity& previous : previousTaskGroups)
        {
            const auto current = std::find_if(currentTaskGroups.begin(),
                currentTaskGroups.end(), [&previous](const HardwareTaskIdentity& group)
                {
                    return group.id == previous.id;
                });
            if (current != currentTaskGroups.end())
            {
                if (current->name.empty() || current->name == previous.name)
                    continue;
                for (HardwareIoMapping& mapping : mappings)
                {
                    if (mapping.signal == HardwareIoSignal::Trigger &&
                        mapping.taskGroupName == previous.name)
                    {
                        mapping.taskGroupName = current->name;
                        changed = true;
                    }
                }
                continue;
            }

            const bool nameReused = std::any_of(currentTaskGroups.begin(),
                currentTaskGroups.end(), [&previous](const HardwareTaskIdentity& group)
                {
                    return group.name == previous.name;
                });
            if (nameReused)
                continue;
            const std::size_t originalSize = mappings.size();
            std::erase_if(mappings, [&previous](const HardwareIoMapping& mapping)
            {
                return mapping.signal == HardwareIoSignal::Trigger &&
                    mapping.taskGroupName == previous.name;
            });
            changed |= mappings.size() != originalSize;
        }
    }

    std::vector<std::string> currentNames;
    currentNames.reserve(currentTaskGroups.size());
    for (const HardwareTaskIdentity& group : currentTaskGroups)
    {
        if (!group.name.empty())
            currentNames.push_back(group.name);
    }
    return EnsureTaskTriggerMappings(mappings, currentNames) || changed;
}

HardwarePanelSettings Load(const std::string& path)
{
    HardwarePanelSettings settings;
    try
    {
        std::ifstream input(ResolveSettingsFile(path), std::ios::binary);
        if (!input)
            return settings;

        nlohmann::json json;
        input >> json;
        const nlohmann::json& camera = json.value("camera", nlohmann::json::object());
        settings.cameraAddress = camera.value("address", settings.cameraAddress);
        settings.cameraSourceName = camera.value("sourceName", settings.cameraSourceName);
        settings.cameraBackend = camera.value("backend", settings.cameraBackend);
        settings.cameraOrientation = camera.value("orientation", settings.cameraOrientation);
        settings.cameraTimeoutMs = camera.value("timeoutMs", settings.cameraTimeoutMs);
        settings.cameraIntervalMs = camera.value("intervalMs", settings.cameraIntervalMs);
        settings.cameraAutoCapture = camera.value("autoCapture", settings.cameraAutoCapture);
        settings.cameraRunAfterCapture = camera.value("runAfterCapture", settings.cameraRunAfterCapture);
        settings.cameraTriggerBeforeRun = camera.value("triggerBeforeRun", settings.cameraTriggerBeforeRun);
        settings.cameraAutoExposure = camera.value("autoExposure", settings.cameraAutoExposure);
        settings.cameraExposure = camera.value("exposure", settings.cameraExposure);
        settings.cameraGain = camera.value("gain", settings.cameraGain);
        settings.cameraAutoReconnect = camera.value("autoReconnect", settings.cameraAutoReconnect);
        settings.cameraReconnectFailureThreshold = camera.value(
            "reconnectFailureThreshold", settings.cameraReconnectFailureThreshold);
        settings.cameraReconnectInitialDelayMs = camera.value(
            "reconnectInitialDelayMs", settings.cameraReconnectInitialDelayMs);
        settings.cameraReconnectMaxDelayMs = camera.value(
            "reconnectMaxDelayMs", settings.cameraReconnectMaxDelayMs);

        const nlohmann::json& output = json.value("output", nlohmann::json::object());
        settings.outputType = output.value("type", settings.outputType);
        settings.outputKey = output.value("adapterKey", settings.outputKey);
        settings.outputAddress = output.value("address", settings.outputAddress);
        settings.outputPort = output.value("port", settings.outputPort);
        settings.outputResource = output.value("resource", settings.outputResource);
        settings.outputTarget = output.value("target", settings.outputTarget);
        settings.outputAddressValue = output.value("mappingAddress", settings.outputAddressValue);
        settings.outputTimeoutMs = output.value("timeoutMs", settings.outputTimeoutMs);
        settings.plcHoldingRegister = output.value("plcHoldingRegister", settings.plcHoldingRegister);
        settings.tcpPassText = output.value("passText", settings.tcpPassText);
        settings.tcpFailText = output.value("failText", settings.tcpFailText);
        settings.tcpAppendCrLf = output.value("appendCrLf", settings.tcpAppendCrLf);
        settings.outputInvert = output.value("invert", settings.outputInvert);
        settings.outputAutoPublish = output.value("autoPublish", settings.outputAutoPublish);
        settings.outputQueueSize = output.value("queueSize", settings.outputQueueSize);
        settings.outputRetryCount = output.value("retryCount", settings.outputRetryCount);
        settings.outputRetryDelayMs = output.value("retryDelayMs", settings.outputRetryDelayMs);
        settings.outputReconnectBeforeRetry = output.value(
            "reconnectBeforeRetry", settings.outputReconnectBeforeRetry);
        settings.outputHandshakeEnabled = output.value(
            "handshakeEnabled", settings.outputHandshakeEnabled);
        settings.outputPollIntervalMs = output.value(
            "pollIntervalMs", settings.outputPollIntervalMs);
        settings.outputAcknowledgementTimeoutMs = output.value(
            "acknowledgementTimeoutMs", settings.outputAcknowledgementTimeoutMs);
        settings.outputInspectionTimeoutMs = output.value(
            "inspectionTimeoutMs", settings.outputInspectionTimeoutMs);
        settings.outputHeartbeatIntervalMs = output.value(
            "heartbeatIntervalMs", settings.outputHeartbeatIntervalMs);
        settings.outputAutoReconnect = output.value(
            "autoReconnect", settings.outputAutoReconnect);
        settings.outputReconnectFailureThreshold = output.value(
            "reconnectFailureThreshold", settings.outputReconnectFailureThreshold);
        settings.outputReconnectInitialDelayMs = output.value(
            "reconnectInitialDelayMs", settings.outputReconnectInitialDelayMs);
        settings.outputReconnectMaxDelayMs = output.value(
            "reconnectMaxDelayMs", settings.outputReconnectMaxDelayMs);
        const auto mappings = output.find("ioMappings");
        if (mappings != output.end() && mappings->is_array())
        {
            settings.outputIoMappings.clear();
            for (const nlohmann::json& item : *mappings)
            {
                HardwareIoMapping mapping;
                mapping.enabled = item.value("enabled", mapping.enabled);
                mapping.signal = static_cast<HardwareIoSignal>(std::clamp(
                    item.value("signal", 0), 0, 7));
                mapping.direction = static_cast<HardwareIoDirection>(std::clamp(
                    item.value("direction", 0), 0, 1));
                mapping.address = static_cast<std::uint16_t>(std::clamp(
                    item.value("address", 0), 0, 65535));
                mapping.invert = item.value("invert", mapping.invert);
                mapping.pulseMs = item.value("pulseMs", mapping.pulseMs);
                mapping.taskGroupName = item.value(
                    "taskGroupName", mapping.taskGroupName);
                settings.outputIoMappings.push_back(std::move(mapping));
            }
        }
    }
    catch (...)
    {
        return HardwarePanelSettings{};
    }
    return Normalize(std::move(settings));
}

bool Save(const HardwarePanelSettings& source, const std::string& path, std::string* error)
{
    try
    {
        const HardwarePanelSettings settings = Normalize(source);
        const fs::path settingsFile = ResolveSettingsFile(path);
        if (!settingsFile.parent_path().empty())
            fs::create_directories(settingsFile.parent_path());

        const nlohmann::json json = {
            {"version", 2},
            {"camera", {
                {"address", settings.cameraAddress},
                {"sourceName", settings.cameraSourceName},
                {"backend", settings.cameraBackend},
                {"orientation", settings.cameraOrientation},
                {"timeoutMs", settings.cameraTimeoutMs},
                {"intervalMs", settings.cameraIntervalMs},
                {"autoCapture", settings.cameraAutoCapture},
                {"runAfterCapture", settings.cameraRunAfterCapture},
                {"triggerBeforeRun", settings.cameraTriggerBeforeRun},
                {"autoExposure", settings.cameraAutoExposure},
                {"exposure", settings.cameraExposure},
                {"gain", settings.cameraGain},
                {"autoReconnect", settings.cameraAutoReconnect},
                {"reconnectFailureThreshold", settings.cameraReconnectFailureThreshold},
                {"reconnectInitialDelayMs", settings.cameraReconnectInitialDelayMs},
                {"reconnectMaxDelayMs", settings.cameraReconnectMaxDelayMs}
            }},
            {"output", {
                {"type", settings.outputType},
                {"adapterKey", settings.outputKey},
                {"address", settings.outputAddress},
                {"port", settings.outputPort},
                {"resource", settings.outputResource},
                {"target", settings.outputTarget},
                {"mappingAddress", settings.outputAddressValue},
                {"timeoutMs", settings.outputTimeoutMs},
                {"plcHoldingRegister", settings.plcHoldingRegister},
                {"passText", settings.tcpPassText},
                {"failText", settings.tcpFailText},
                {"appendCrLf", settings.tcpAppendCrLf},
                {"invert", settings.outputInvert},
                {"autoPublish", settings.outputAutoPublish},
                {"queueSize", settings.outputQueueSize},
                {"retryCount", settings.outputRetryCount},
                {"retryDelayMs", settings.outputRetryDelayMs},
                {"reconnectBeforeRetry", settings.outputReconnectBeforeRetry},
                {"handshakeEnabled", settings.outputHandshakeEnabled},
                {"pollIntervalMs", settings.outputPollIntervalMs},
                {"acknowledgementTimeoutMs", settings.outputAcknowledgementTimeoutMs},
                {"inspectionTimeoutMs", settings.outputInspectionTimeoutMs},
                {"heartbeatIntervalMs", settings.outputHeartbeatIntervalMs},
                {"autoReconnect", settings.outputAutoReconnect},
                {"reconnectFailureThreshold", settings.outputReconnectFailureThreshold},
                {"reconnectInitialDelayMs", settings.outputReconnectInitialDelayMs},
                {"reconnectMaxDelayMs", settings.outputReconnectMaxDelayMs},
                {"ioMappings", [&settings]
                {
                    nlohmann::json mappings = nlohmann::json::array();
                    for (const HardwareIoMapping& mapping : settings.outputIoMappings)
                    {
                        mappings.push_back({
                            {"enabled", mapping.enabled},
                            {"signal", static_cast<int>(mapping.signal)},
                            {"direction", static_cast<int>(mapping.direction)},
                            {"address", mapping.address},
                            {"invert", mapping.invert},
                            {"pulseMs", mapping.pulseMs},
                            {"taskGroupName", mapping.taskGroupName}
                        });
                    }
                    return mappings;
                }()}
            }}
        };

        std::ofstream output(settingsFile, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            if (error)
                *error = "cannot open hardware settings file";
            return false;
        }
        output << json.dump(2);
        if (!output)
        {
            if (error)
                *error = "cannot write hardware settings file";
            return false;
        }
        if (error)
            error->clear();
        return true;
    }
    catch (const std::exception& exception)
    {
        if (error)
            *error = exception.what();
        return false;
    }
}

std::string SettingsPath()
{
    return PathToUtf8(DefaultSettingsFile());
}
}
