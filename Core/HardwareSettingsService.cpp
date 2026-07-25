#include "HardwareSettingsService.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
    return settings;
}
}

namespace HardwareSettingsService
{
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
            {"version", 1},
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
                {"reconnectBeforeRetry", settings.outputReconnectBeforeRetry}
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
