#pragma once

#include "HardwareRuntimeService.h"

#include <cstdint>
#include <string>
#include <vector>

struct HardwareTaskIdentity
{
    std::uint64_t id = 0;
    std::string name;
    bool operator==(const HardwareTaskIdentity&) const = default;
};

struct HardwarePanelSettings
{
    std::string cameraAddress = "0";
    std::string cameraSourceName = "industrial-camera";
    int cameraBackend = 0;
    int cameraOrientation = 0;
    int cameraTimeoutMs = 250;
    int cameraIntervalMs = 33;
    bool cameraAutoCapture = true;
    bool cameraRunAfterCapture = true;
    bool cameraTriggerBeforeRun = true;
    bool cameraAutoExposure = true;
    float cameraExposure = -6.0f;
    float cameraGain = 0.0f;
    bool cameraAutoReconnect = true;
    int cameraReconnectFailureThreshold = 3;
    int cameraReconnectInitialDelayMs = 250;
    int cameraReconnectMaxDelayMs = 5000;

    int outputType = 0;
    std::string outputKey = "output-main";
    std::string outputAddress = "127.0.0.1";
    int outputPort = 502;
    std::string outputResource = "1";
    std::string outputTarget = "ns=2;s=Inspection.OK";
    int outputAddressValue = 0;
    int outputTimeoutMs = 1500;
    bool plcHoldingRegister = false;
    std::string tcpPassText = "PASS";
    std::string tcpFailText = "FAIL";
    bool tcpAppendCrLf = true;
    bool outputInvert = false;
    bool outputAutoPublish = false;
    int outputQueueSize = 32;
    int outputRetryCount = 2;
    int outputRetryDelayMs = 150;
    bool outputReconnectBeforeRetry = true;
    bool outputHandshakeEnabled = false;
    int outputPollIntervalMs = 50;
    int outputAcknowledgementTimeoutMs = 3000;
    int outputInspectionTimeoutMs = 30000;
    int outputHeartbeatIntervalMs = 1000;
    bool outputAutoReconnect = true;
    int outputReconnectFailureThreshold = 3;
    int outputReconnectInitialDelayMs = 250;
    int outputReconnectMaxDelayMs = 5000;
    std::vector<HardwareIoMapping> outputIoMappings = {
        {true, HardwareIoSignal::Trigger, HardwareIoDirection::Input, 0, false, 0, "任务01"},
        {true, HardwareIoSignal::Busy, HardwareIoDirection::Output, 1, false, 0, {}},
        {true, HardwareIoSignal::Done, HardwareIoDirection::Output, 2, false, 200, {}},
        {true, HardwareIoSignal::Ok, HardwareIoDirection::Output, 3, false, 0, {}},
        {true, HardwareIoSignal::Ng, HardwareIoDirection::Output, 4, false, 0, {}},
        {true, HardwareIoSignal::Error, HardwareIoDirection::Output, 5, false, 0, {}},
        {true, HardwareIoSignal::Heartbeat, HardwareIoDirection::Output, 6, false, 0, {}},
        {true, HardwareIoSignal::Acknowledge, HardwareIoDirection::Input, 7, false, 0, {}}
    };
};

namespace HardwareSettingsService
{
    std::vector<HardwareIoMapping> BuildStandardIoMappings(
        const std::vector<std::string>& taskGroupNames);
    bool EnsureTaskTriggerMappings(std::vector<HardwareIoMapping>& mappings,
        const std::vector<std::string>& taskGroupNames);
    bool SynchronizeTaskTriggerMappings(std::vector<HardwareIoMapping>& mappings,
        const std::vector<HardwareTaskIdentity>& previousTaskGroups,
        const std::vector<HardwareTaskIdentity>& currentTaskGroups);
    HardwarePanelSettings Load(const std::string& path = {});
    bool Save(const HardwarePanelSettings& settings, const std::string& path = {},
        std::string* error = nullptr);
    std::string SettingsPath();
}
