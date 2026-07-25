#pragma once

#include <string>

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
};

namespace HardwareSettingsService
{
    HardwarePanelSettings Load(const std::string& path = {});
    bool Save(const HardwarePanelSettings& settings, const std::string& path = {},
        std::string* error = nullptr);
    std::string SettingsPath();
}
