#pragma once

#include "HardwareAdapters.h"

#include <memory>
#include <mutex>
#include <string>

class ICameraCaptureBackend
{
public:
    virtual ~ICameraCaptureBackend() = default;
    virtual DeviceOperationResult Open(const DeviceEndpoint& endpoint) = 0;
    virtual void Close() = 0;
    virtual DeviceOperationResult Read(cv::Mat& frame, int timeoutMs) = 0;
};

class OpenCvCameraAdapter final : public ICameraAdapter
{
public:
    explicit OpenCvCameraAdapter(std::unique_ptr<ICameraCaptureBackend> backend = {});
    ~OpenCvCameraAdapter() override;

    const char* AdapterName() const override;
    DeviceOperationResult Connect(const DeviceEndpoint& endpoint) override;
    void Disconnect() override;
    DeviceConnectionState ConnectionState() const override;
    std::string LastError() const override;

    DeviceOperationResult GrabFrame(cv::Mat& frame, int timeoutMs) override;
    DeviceOperationResult StartStream() override;
    void StopStream() override;

    bool IsStreaming() const;

private:
    DeviceOperationResult Fail(std::string message, bool fault = false);

    mutable std::mutex mutex_;
    std::unique_ptr<ICameraCaptureBackend> backend_;
    DeviceConnectionState state_ = DeviceConnectionState::Disconnected;
    std::string lastError_;
    bool streaming_ = false;
};
