#include "HardwareSettingsService.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <utility>
#include <windows.h>

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

HardwareCameraSettings LegacyCameraSettings(const HardwarePanelSettings& settings)
{
    HardwareCameraSettings camera;
    camera.address = settings.cameraAddress;
    camera.sourceName = settings.cameraSourceName;
    camera.backend = settings.cameraBackend;
    camera.orientation = settings.cameraOrientation;
    camera.timeoutMs = settings.cameraTimeoutMs;
    camera.intervalMs = settings.cameraIntervalMs;
    camera.autoCapture = settings.cameraAutoCapture;
    camera.runAfterCapture = settings.cameraRunAfterCapture;
    camera.triggerBeforeRun = settings.cameraTriggerBeforeRun;
    camera.autoExposure = settings.cameraAutoExposure;
    camera.exposure = settings.cameraExposure;
    camera.gain = settings.cameraGain;
    camera.autoReconnect = settings.cameraAutoReconnect;
    camera.reconnectFailureThreshold = settings.cameraReconnectFailureThreshold;
    camera.reconnectInitialDelayMs = settings.cameraReconnectInitialDelayMs;
    camera.reconnectMaxDelayMs = settings.cameraReconnectMaxDelayMs;
    return camera;
}

void NormalizeCamera(HardwareCameraSettings& camera, std::size_t index)
{
    if (camera.address.empty())
        camera.address = std::to_string(index);
    if (camera.sourceName.empty())
    {
        char sourceName[32];
        std::snprintf(sourceName, sizeof(sourceName), "camera-%02zu", index + 1);
        camera.sourceName = sourceName;
    }
    camera.backend = std::clamp(camera.backend, 0, 6);
    camera.orientation = std::clamp(camera.orientation, 0, 5);
    camera.timeoutMs = std::clamp(camera.timeoutMs, 1, 10000);
    camera.intervalMs = std::clamp(camera.intervalMs, 1, 10000);
    camera.exposure = camera.backend >= 5
        ? std::clamp(camera.exposure, 1.0f, 1000000.0f)
        : std::clamp(camera.exposure, -13.0f, 5.0f);
    camera.gain = std::clamp(camera.gain, 0.0f, 100.0f);
    camera.triggerMode = std::clamp(camera.triggerMode, 0, 3);
    camera.bufferPolicy = std::clamp(camera.bufferPolicy, 0, 1);
    camera.triggerDelayMicroseconds = std::clamp(
        camera.triggerDelayMicroseconds, 0.0f, 1000000.0f);
    camera.reconnectFailureThreshold = std::clamp(
        camera.reconnectFailureThreshold, 1, 100);
    camera.reconnectInitialDelayMs = std::clamp(
        camera.reconnectInitialDelayMs, 1, 60000);
    camera.reconnectMaxDelayMs = std::clamp(
        camera.reconnectMaxDelayMs, camera.reconnectInitialDelayMs, 60000);
}

void SyncLegacyCameraFields(HardwarePanelSettings& settings)
{
    const HardwareCameraSettings& camera = settings.cameras.front();
    settings.cameraAddress = camera.address;
    settings.cameraSourceName = camera.sourceName;
    settings.cameraBackend = camera.backend;
    settings.cameraOrientation = camera.orientation;
    settings.cameraTimeoutMs = camera.timeoutMs;
    settings.cameraIntervalMs = camera.intervalMs;
    settings.cameraAutoCapture = camera.autoCapture;
    settings.cameraRunAfterCapture = camera.runAfterCapture;
    settings.cameraTriggerBeforeRun = camera.triggerBeforeRun;
    settings.cameraAutoExposure = camera.autoExposure;
    settings.cameraExposure = camera.exposure;
    settings.cameraGain = camera.gain;
    settings.cameraAutoReconnect = camera.autoReconnect;
    settings.cameraReconnectFailureThreshold = camera.reconnectFailureThreshold;
    settings.cameraReconnectInitialDelayMs = camera.reconnectInitialDelayMs;
    settings.cameraReconnectMaxDelayMs = camera.reconnectMaxDelayMs;
}

HardwareOutputConnectionConfig DefaultAuxiliaryOutput(std::size_t index)
{
    HardwareOutputConnectionConfig config;
    config.enabled = false;
    config.adapterType = HardwareOutputAdapterType::TcpText;
    config.endpoint.address = "127.0.0.1";
    config.endpoint.port = static_cast<std::uint16_t>(5001 + index);
    config.endpoint.timeoutMs = 1500;
    char key[32];
    std::snprintf(key, sizeof(key), "output-aux-%02zu", index + 1);
    config.binding.adapterKey = key;
    config.binding.passText = "PASS";
    config.binding.failText = "FAIL";
    config.binding.appendCrLf = true;
    config.autoPublish = true;
    config.handshake.enabled = false;
    return config;
}

void NormalizeAuxiliaryOutput(HardwareOutputConnectionConfig& config,
    std::size_t index)
{
    const int type = std::clamp(static_cast<int>(config.adapterType), 0, 3);
    config.adapterType = static_cast<HardwareOutputAdapterType>(type);
    if (config.binding.adapterKey.empty())
        config.binding.adapterKey = DefaultAuxiliaryOutput(index).binding.adapterKey;
    config.endpoint.timeoutMs = std::clamp(config.endpoint.timeoutMs, 1, 60000);
    config.maxQueueSize = std::clamp(config.maxQueueSize, 1, 1024);
    config.retryCount = std::clamp(config.retryCount, 0, 10);
    config.retryDelayMs = std::clamp(config.retryDelayMs, 1, 60000);
    config.handshake.enabled = false;
}

HardwarePanelSettings Normalize(HardwarePanelSettings settings)
{
    if (settings.cameras.empty())
    {
        settings.cameras.resize(kHardwareCameraCount);
        settings.cameras[0] = LegacyCameraSettings(settings);
        for (std::size_t index = 1; index < settings.cameras.size(); ++index)
        {
            settings.cameras[index].address = std::to_string(index);
            char sourceName[32];
            std::snprintf(sourceName, sizeof(sourceName), "camera-%02zu", index + 1);
            settings.cameras[index].sourceName = sourceName;
        }
    }
    const std::size_t configuredCameraCount = settings.cameras.size();
    settings.cameras.resize(kHardwareCameraCount);
    for (std::size_t index = configuredCameraCount; index < settings.cameras.size(); ++index)
    {
        settings.cameras[index].address = std::to_string(index);
        char sourceName[32];
        std::snprintf(sourceName, sizeof(sourceName), "camera-%02zu", index + 1);
        settings.cameras[index].sourceName = sourceName;
    }
    for (std::size_t index = 0; index < settings.cameras.size(); ++index)
        NormalizeCamera(settings.cameras[index], index);
    settings.activeCameraIndex = std::clamp(
        settings.activeCameraIndex, 0, static_cast<int>(kHardwareCameraCount) - 1);
    settings.cameraMaxConcurrentGrabs = std::clamp(
        settings.cameraMaxConcurrentGrabs, 1,
        static_cast<int>(kHardwareCameraCount));
    SyncLegacyCameraFields(settings);
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
    const std::size_t configuredAuxiliaryCount = settings.auxiliaryOutputs.size();
    settings.auxiliaryOutputs.resize(kHardwareAuxiliaryOutputCount);
    for (std::size_t index = configuredAuxiliaryCount;
        index < settings.auxiliaryOutputs.size(); ++index)
    {
        settings.auxiliaryOutputs[index] = DefaultAuxiliaryOutput(index);
    }
    for (std::size_t index = 0; index < settings.auxiliaryOutputs.size(); ++index)
        NormalizeAuxiliaryOutput(settings.auxiliaryOutputs[index], index);
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

std::uint16_t DefaultTaskResultAddress(HardwareIoSignal signal,
    std::size_t taskIndex)
{
    // 任务01沿用旧 OK=3、NG=4；任务02起使用 23 之后的独立地址。
    if (taskIndex == 0)
        return signal == HardwareIoSignal::Ok ? 3 : 4;
    return static_cast<std::uint16_t>(signal == HardwareIoSignal::Ok
        ? 22 + taskIndex : 37 + taskIndex);
}

bool IsTaskScopedSignal(HardwareIoSignal signal)
{
    return signal == HardwareIoSignal::Trigger ||
        signal == HardwareIoSignal::Ok || signal == HardwareIoSignal::Ng;
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
    mappings.reserve(names.size() * 3 + 5);
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        mappings.push_back({true, HardwareIoSignal::Trigger,
            HardwareIoDirection::Input, DefaultTriggerAddress(index),
            false, 0, names[index]});
        mappings.push_back({true, HardwareIoSignal::Ok,
            HardwareIoDirection::Output,
            DefaultTaskResultAddress(HardwareIoSignal::Ok, index),
            false, 0, names[index]});
        mappings.push_back({true, HardwareIoSignal::Ng,
            HardwareIoDirection::Output,
            DefaultTaskResultAddress(HardwareIoSignal::Ng, index),
            false, 0, names[index]});
    }

    const HardwarePanelSettings defaults;
    for (const HardwareIoMapping& mapping : defaults.outputIoMappings)
    {
        if (!IsTaskScopedSignal(mapping.signal))
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
    std::set<std::pair<HardwareIoSignal, std::string>> boundTaskSignals;
    bool changed = false;
    // 旧配置只有一组全局 OK/NG：迁移为任务01，保留原 PLC 地址。
    for (HardwareIoSignal signal : {HardwareIoSignal::Ok, HardwareIoSignal::Ng})
    {
        const auto legacy = std::find_if(mappings.begin(), mappings.end(),
            [signal](const HardwareIoMapping& mapping)
            {
                return mapping.signal == signal && mapping.taskGroupName.empty();
            });
        if (legacy != mappings.end())
        {
            legacy->taskGroupName = names.front();
            changed = true;
        }
    }
    for (const HardwareIoMapping& mapping : mappings)
    {
        occupiedAddresses.insert(mapping.address);
        if (mapping.signal == HardwareIoSignal::Trigger &&
            !mapping.taskGroupName.empty())
        {
            boundTasks.insert(mapping.taskGroupName);
        }
        if (IsTaskScopedSignal(mapping.signal) && !mapping.taskGroupName.empty())
            boundTaskSignals.insert({mapping.signal, mapping.taskGroupName});
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
        boundTaskSignals.insert({HardwareIoSignal::Trigger, names[index]});
    }
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        for (HardwareIoSignal signal : {HardwareIoSignal::Ok, HardwareIoSignal::Ng})
        {
            if (boundTaskSignals.contains({signal, names[index]}))
                continue;
            const std::uint16_t address = FindAvailableAddress(occupiedAddresses,
                DefaultTaskResultAddress(signal, index));
            additions.push_back({true, signal, HardwareIoDirection::Output,
                address, false, 0, names[index]});
            occupiedAddresses.insert(address);
            boundTaskSignals.insert({signal, names[index]});
        }
    }
    if (additions.empty())
        return changed;

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
                    if (IsTaskScopedSignal(mapping.signal) &&
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
                return IsTaskScopedSignal(mapping.signal) &&
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
    const fs::path settingsFile = ResolveSettingsFile(path);
    try
    {
        std::ifstream input(settingsFile, std::ios::binary);
        if (!input)
        {
            const fs::path backup = settingsFile.wstring() + L".bak";
            return settingsFile.extension() != L".bak" && fs::exists(backup)
                ? Load(PathToUtf8(backup)) : Normalize(std::move(settings));
        }

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

        settings.activeCameraIndex = json.value(
            "activeCameraIndex", settings.activeCameraIndex);
        settings.cameraMaxConcurrentGrabs = json.value(
            "cameraMaxConcurrentGrabs", settings.cameraMaxConcurrentGrabs);
        const auto cameras = json.find("cameras");
        if (cameras != json.end() && cameras->is_array())
        {
            settings.cameras.clear();
            for (const nlohmann::json& item : *cameras)
            {
                if (!item.is_object() || settings.cameras.size() >= kHardwareCameraCount)
                    break;
                HardwareCameraSettings cameraSettings;
                cameraSettings.connectOnStartup = item.value(
                    "connectOnStartup", cameraSettings.connectOnStartup);
                cameraSettings.address = item.value("address", cameraSettings.address);
                cameraSettings.sourceName = item.value("sourceName", cameraSettings.sourceName);
                cameraSettings.backend = item.value("backend", cameraSettings.backend);
                cameraSettings.orientation = item.value("orientation", cameraSettings.orientation);
                cameraSettings.timeoutMs = item.value("timeoutMs", cameraSettings.timeoutMs);
                cameraSettings.intervalMs = item.value("intervalMs", cameraSettings.intervalMs);
                cameraSettings.autoCapture = item.value("autoCapture", cameraSettings.autoCapture);
                cameraSettings.runAfterCapture = item.value(
                    "runAfterCapture", cameraSettings.runAfterCapture);
                cameraSettings.triggerBeforeRun = item.value(
                    "triggerBeforeRun", cameraSettings.triggerBeforeRun);
                cameraSettings.autoExposure = item.value("autoExposure", cameraSettings.autoExposure);
                cameraSettings.exposure = item.value("exposure", cameraSettings.exposure);
                cameraSettings.gain = item.value("gain", cameraSettings.gain);
                cameraSettings.triggerMode = item.value(
                    "triggerMode", cameraSettings.triggerMode);
                cameraSettings.triggerDelayMicroseconds = item.value(
                    "triggerDelayMicroseconds", cameraSettings.triggerDelayMicroseconds);
                cameraSettings.bufferPolicy = item.value(
                    "bufferPolicy", cameraSettings.bufferPolicy);
                cameraSettings.ptpEnabled = item.value(
                    "ptpEnabled", cameraSettings.ptpEnabled);
                cameraSettings.autoReconnect = item.value(
                    "autoReconnect", cameraSettings.autoReconnect);
                cameraSettings.reconnectFailureThreshold = item.value(
                    "reconnectFailureThreshold", cameraSettings.reconnectFailureThreshold);
                cameraSettings.reconnectInitialDelayMs = item.value(
                    "reconnectInitialDelayMs", cameraSettings.reconnectInitialDelayMs);
                cameraSettings.reconnectMaxDelayMs = item.value(
                    "reconnectMaxDelayMs", cameraSettings.reconnectMaxDelayMs);
                settings.cameras.push_back(std::move(cameraSettings));
            }
        }

        const auto auxiliaryOutputs = json.find("auxiliaryOutputs");
        if (auxiliaryOutputs != json.end() && auxiliaryOutputs->is_array())
        {
            settings.auxiliaryOutputs.clear();
            for (const nlohmann::json& item : *auxiliaryOutputs)
            {
                if (!item.is_object() ||
                    settings.auxiliaryOutputs.size() >= kHardwareAuxiliaryOutputCount)
                {
                    break;
                }
                const std::size_t index = settings.auxiliaryOutputs.size();
                HardwareOutputConnectionConfig config = DefaultAuxiliaryOutput(index);
                config.enabled = item.value("enabled", config.enabled);
                config.adapterType = static_cast<HardwareOutputAdapterType>(std::clamp(
                    item.value("type", static_cast<int>(config.adapterType)), 0, 3));
                config.binding.adapterKey = item.value(
                    "adapterKey", config.binding.adapterKey);
                config.endpoint.address = item.value("address", config.endpoint.address);
                config.endpoint.port = static_cast<std::uint16_t>(std::clamp(
                    item.value("port", static_cast<int>(config.endpoint.port)), 0, 65535));
                config.endpoint.resource = item.value("resource", config.endpoint.resource);
                config.endpoint.timeoutMs = item.value("timeoutMs", config.endpoint.timeoutMs);
                config.binding.target = item.value("target", config.binding.target);
                config.binding.address = static_cast<std::uint16_t>(std::clamp(
                    item.value("mappingAddress", static_cast<int>(config.binding.address)),
                    0, 65535));
                config.plcUseHoldingRegister = item.value(
                    "plcHoldingRegister", config.plcUseHoldingRegister);
                config.binding.passText = item.value("passText", config.binding.passText);
                config.binding.failText = item.value("failText", config.binding.failText);
                config.binding.sendQrJson = item.value("sendQrJson", config.binding.sendQrJson);
                config.binding.appendCrLf = item.value(
                    "appendCrLf", config.binding.appendCrLf);
                config.binding.invert = item.value("invert", config.binding.invert);
                config.autoPublish = item.value("autoPublish", config.autoPublish);
                config.maxQueueSize = item.value("queueSize", config.maxQueueSize);
                config.retryCount = item.value("retryCount", config.retryCount);
                config.retryDelayMs = item.value("retryDelayMs", config.retryDelayMs);
                config.reconnectBeforeRetry = item.value(
                    "reconnectBeforeRetry", config.reconnectBeforeRetry);
                settings.auxiliaryOutputs.push_back(std::move(config));
            }
        }

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
        settings.tcpSendQrJson = output.value("sendQrJson", settings.tcpSendQrJson);
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
        const fs::path backup = settingsFile.wstring() + L".bak";
        return settingsFile.extension() != L".bak" && fs::exists(backup)
            ? Load(PathToUtf8(backup)) : Normalize(HardwarePanelSettings{});
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
            {"version", 7},
            {"activeCameraIndex", settings.activeCameraIndex},
            {"cameraMaxConcurrentGrabs", settings.cameraMaxConcurrentGrabs},
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
            {"cameras", [&settings]
            {
                nlohmann::json cameras = nlohmann::json::array();
                for (const HardwareCameraSettings& camera : settings.cameras)
                {
                    cameras.push_back({
                        {"connectOnStartup", camera.connectOnStartup},
                        {"address", camera.address},
                        {"sourceName", camera.sourceName},
                        {"backend", camera.backend},
                        {"orientation", camera.orientation},
                        {"timeoutMs", camera.timeoutMs},
                        {"intervalMs", camera.intervalMs},
                        {"autoCapture", camera.autoCapture},
                        {"runAfterCapture", camera.runAfterCapture},
                        {"triggerBeforeRun", camera.triggerBeforeRun},
                        {"autoExposure", camera.autoExposure},
                        {"exposure", camera.exposure},
                        {"gain", camera.gain},
                        {"triggerMode", camera.triggerMode},
                        {"triggerDelayMicroseconds", camera.triggerDelayMicroseconds},
                        {"bufferPolicy", camera.bufferPolicy},
                        {"ptpEnabled", camera.ptpEnabled},
                        {"autoReconnect", camera.autoReconnect},
                        {"reconnectFailureThreshold", camera.reconnectFailureThreshold},
                        {"reconnectInitialDelayMs", camera.reconnectInitialDelayMs},
                        {"reconnectMaxDelayMs", camera.reconnectMaxDelayMs}
                    });
                }
                return cameras;
            }()},
            {"auxiliaryOutputs", [&settings]
            {
                nlohmann::json outputs = nlohmann::json::array();
                for (const HardwareOutputConnectionConfig& config :
                    settings.auxiliaryOutputs)
                {
                    outputs.push_back({
                        {"enabled", config.enabled},
                        {"type", static_cast<int>(config.adapterType)},
                        {"adapterKey", config.binding.adapterKey},
                        {"address", config.endpoint.address},
                        {"port", config.endpoint.port},
                        {"resource", config.endpoint.resource},
                        {"timeoutMs", config.endpoint.timeoutMs},
                        {"target", config.binding.target},
                        {"mappingAddress", config.binding.address},
                        {"plcHoldingRegister", config.plcUseHoldingRegister},
                        {"passText", config.binding.passText},
                        {"failText", config.binding.failText},
                        {"sendQrJson", config.binding.sendQrJson},
                        {"appendCrLf", config.binding.appendCrLf},
                        {"invert", config.binding.invert},
                        {"autoPublish", config.autoPublish},
                        {"queueSize", config.maxQueueSize},
                        {"retryCount", config.retryCount},
                        {"retryDelayMs", config.retryDelayMs},
                        {"reconnectBeforeRetry", config.reconnectBeforeRetry}
                    });
                }
                return outputs;
            }()},
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
                {"sendQrJson", settings.tcpSendQrJson},
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

        const fs::path temporaryFile = settingsFile.wstring() + L".tmp";
        const fs::path backupFile = settingsFile.wstring() + L".bak";
        std::ofstream output(temporaryFile, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            if (error)
                *error = "cannot open hardware settings file";
            return false;
        }
        const std::string serialized = json.dump(2);
        output.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
        output.flush();
        if (!output)
        {
            if (error)
                *error = "cannot write hardware settings file";
            return false;
        }
        output.close();

        // Read the temporary file back before replacing the last known-good copy.
        std::ifstream validationInput(temporaryFile, std::ios::binary);
        nlohmann::json validation;
        validationInput >> validation;
        if (!validationInput || !validation.is_object() ||
            !validation.contains("camera") || !validation.contains("output"))
            throw std::runtime_error("hardware settings validation failed");
        validationInput.close();

        if (fs::exists(settingsFile) &&
            !CopyFileW(settingsFile.c_str(), backupFile.c_str(), FALSE))
            throw std::runtime_error("cannot create hardware settings backup");
        if (!MoveFileExW(temporaryFile.c_str(), settingsFile.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("cannot atomically replace hardware settings");
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

bool RestoreLastValid(const std::string& path, std::string* error)
{
    try
    {
        const fs::path settingsFile = ResolveSettingsFile(path);
        const fs::path backupFile = settingsFile.wstring() + L".bak";
        const fs::path temporaryFile = settingsFile.wstring() + L".tmp";
        if (!fs::exists(backupFile))
            throw std::runtime_error("hardware settings backup does not exist");

        std::ifstream input(backupFile, std::ios::binary);
        nlohmann::json validation;
        input >> validation;
        if (!input || !validation.is_object() || !validation.contains("camera") ||
            !validation.contains("output"))
            throw std::runtime_error("hardware settings backup is invalid");
        input.close();

        if (!CopyFileW(backupFile.c_str(), temporaryFile.c_str(), FALSE))
            throw std::runtime_error("cannot stage hardware settings backup");
        if (!MoveFileExW(temporaryFile.c_str(), settingsFile.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("cannot restore hardware settings backup");
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
