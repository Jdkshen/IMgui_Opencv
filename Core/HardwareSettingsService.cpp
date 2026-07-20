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
    settings.cameraTimeoutMs = std::clamp(settings.cameraTimeoutMs, 1, 10000);
    settings.cameraIntervalMs = std::clamp(settings.cameraIntervalMs, 1, 10000);
    settings.cameraExposure = std::clamp(settings.cameraExposure, -13.0f, 5.0f);
    settings.cameraGain = std::clamp(settings.cameraGain, 0.0f, 100.0f);
    settings.outputType = std::clamp(settings.outputType, 0, 3);
    settings.outputPort = std::clamp(settings.outputPort, 0, 65535);
    settings.outputAddressValue = std::clamp(settings.outputAddressValue, 0, 65535);
    settings.outputTimeoutMs = std::clamp(settings.outputTimeoutMs, 1, 60000);
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
        settings.cameraTimeoutMs = camera.value("timeoutMs", settings.cameraTimeoutMs);
        settings.cameraIntervalMs = camera.value("intervalMs", settings.cameraIntervalMs);
        settings.cameraAutoCapture = camera.value("autoCapture", settings.cameraAutoCapture);
        settings.cameraRunAfterCapture = camera.value("runAfterCapture", settings.cameraRunAfterCapture);
        settings.cameraTriggerBeforeRun = camera.value("triggerBeforeRun", settings.cameraTriggerBeforeRun);
        settings.cameraAutoExposure = camera.value("autoExposure", settings.cameraAutoExposure);
        settings.cameraExposure = camera.value("exposure", settings.cameraExposure);
        settings.cameraGain = camera.value("gain", settings.cameraGain);

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
                {"timeoutMs", settings.cameraTimeoutMs},
                {"intervalMs", settings.cameraIntervalMs},
                {"autoCapture", settings.cameraAutoCapture},
                {"runAfterCapture", settings.cameraRunAfterCapture},
                {"triggerBeforeRun", settings.cameraTriggerBeforeRun},
                {"autoExposure", settings.cameraAutoExposure},
                {"exposure", settings.cameraExposure},
                {"gain", settings.cameraGain}
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
                {"autoPublish", settings.outputAutoPublish}
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
