#pragma once

#include "HardwareAdapters.h"

#include <memory>
#include <mutex>
#include <string>

// Huaray/iRAYPLE IMV adapter. MVSDKmd.dll is loaded at runtime so the
// application remains usable when the vendor runtime is not installed.
class HuarayImvCameraAdapter final : public ICameraAdapter
{
public:
    HuarayImvCameraAdapter();
    ~HuarayImvCameraAdapter() override;

    const char* AdapterName() const override;
    DeviceOperationResult Connect(const DeviceEndpoint& endpoint) override;
    void Disconnect() override;
    DeviceConnectionState ConnectionState() const override;
    std::string LastError() const override;

    DeviceOperationResult GrabFrame(cv::Mat& frame, int timeoutMs) override;
    DeviceOperationResult GrabFrame(cv::Mat& frame,
        CameraFrameMetadata& metadata, int timeoutMs) override;
    DeviceOperationResult StartStream() override;
    void StopStream() override;
    DeviceOperationResult SetControl(CameraControl control, double value) override;
    CameraCapabilities Capabilities() const override;
    DeviceOperationResult ConfigureTrigger(const CameraTriggerConfig& config) override;
    DeviceOperationResult ExecuteSoftwareTrigger() override;
    DeviceOperationResult ConfigurePtp(bool enabled) override;
    DeviceOperationResult FlushQueue() override;
    CameraStatistics Statistics() const override;
    DeviceOperationResult EnumerateDevices(std::vector<CameraDeviceInfo>& devices) override;
    DeviceOperationResult ForceIp(const std::string& selector,
        const std::string& ipAddress, const std::string& subnetMask,
        const std::string& defaultGateway) override;

private:
    struct Impl;

    DeviceOperationResult Fail(std::string message, bool fault = false);
    void DisconnectUnlocked();

    mutable std::mutex mutex_;
    std::unique_ptr<Impl> impl_;
    DeviceConnectionState state_ = DeviceConnectionState::Disconnected;
    std::string lastError_;
    bool streaming_ = false;
    CameraTriggerConfig triggerConfig_;
    CameraStatistics statistics_;
    std::uint64_t lastVendorFrameNumber_ = 0;
};
