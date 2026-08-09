#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <opencv2/core/mat.hpp>

enum class DeviceConnectionState
{
    Disconnected,
    Connecting,
    Connected,
    Fault
};

struct DeviceEndpoint
{
    std::string address;
    std::uint16_t port = 0;
    std::string resource;
    int timeoutMs = 2000;
};

struct DeviceOperationResult
{
    bool success = false;
    std::string message;
};

enum class CameraControl
{
    AutoExposure,
    Exposure,
    Gain
};

enum class CameraTriggerMode
{
    Continuous,
    Software,
    HardwareLine1,
    HardwareLine2
};

enum class CameraBufferPolicy
{
    Sequential = 0,
    LatestFrame = 1
};

struct CameraTriggerConfig
{
    CameraTriggerMode mode = CameraTriggerMode::Continuous;
    double delayMicroseconds = 0.0;
};

struct CameraFrameMetadata
{
    std::uint64_t frameNumber = 0;
    std::uint64_t hardwareTimestampNanoseconds = 0;
    std::uint64_t receivedTimestampNanoseconds = 0;
    std::uint64_t droppedFrames = 0;
    bool exposureComplete = false;
    std::uint32_t sourcePixelFormat = 0;
    std::string sourcePixelFormatName;
    int sourceBitDepth = 0;
    int sourceStorageBitsPerPixel = 0;
    bool sourceIsBayer = false;
    bool convertedToDisplay = false;
    std::string conversionPath;
};

struct CameraStatistics
{
    std::uint64_t receivedFrames = 0;
    std::uint64_t droppedFrames = 0;
    std::uint64_t incompleteFrames = 0;
    std::uint32_t queuedFrames = 0;
};

struct CameraCapabilities
{
    bool softwareTrigger = false;
    bool hardwareTrigger = false;
    bool hardwareTimestamp = false;
    bool exposureCompletion = false;
    bool queueControl = false;
    bool ptp = false;
};

struct CameraDeviceInfo
{
    std::string selector;
    std::string model;
    std::string serialNumber;
    std::string ipAddress;
    std::string macAddress;
    std::string userDefinedName;
    std::string transport;
    std::string runtimePath;
    std::string runtimeVersion;
    bool accessible = true;
    std::string status;
};

using DeviceValue = std::variant<bool, std::int64_t, double, std::string>;

class IDeviceAdapter
{
public:
    virtual ~IDeviceAdapter() = default;
    virtual const char* AdapterName() const = 0;
    virtual DeviceOperationResult Connect(const DeviceEndpoint& endpoint) = 0;
    virtual void Disconnect() = 0;
    virtual DeviceConnectionState ConnectionState() const = 0;
    virtual std::string LastError() const = 0;
};

class ICameraAdapter : public IDeviceAdapter
{
public:
    virtual DeviceOperationResult GrabFrame(cv::Mat& frame, int timeoutMs) = 0;
    virtual DeviceOperationResult GrabFrame(cv::Mat& frame,
        CameraFrameMetadata& metadata, int timeoutMs)
    {
        metadata = {};
        return GrabFrame(frame, timeoutMs);
    }
    virtual DeviceOperationResult StartStream() = 0;
    virtual void StopStream() = 0;
    virtual DeviceOperationResult SetControl(CameraControl, double)
    {
        return {false, "camera controls are not supported by this adapter"};
    }
    virtual CameraCapabilities Capabilities() const { return {}; }
    virtual DeviceOperationResult ConfigureTrigger(const CameraTriggerConfig&)
    {
        return {false, "camera trigger configuration is not supported by this adapter"};
    }
    virtual DeviceOperationResult ExecuteSoftwareTrigger()
    {
        return {false, "software trigger is not supported by this adapter"};
    }
    virtual DeviceOperationResult FlushQueue()
    {
        return {false, "camera queue control is not supported by this adapter"};
    }
    virtual DeviceOperationResult ConfigureBufferPolicy(CameraBufferPolicy)
    {
        return {false, "camera frame buffer policy is not supported by this adapter"};
    }
    virtual CameraStatistics Statistics() const { return {}; }
    virtual DeviceOperationResult EnumerateDevices(std::vector<CameraDeviceInfo>& devices)
    {
        devices.clear();
        return {false, "camera device discovery is not supported by this adapter"};
    }
    virtual DeviceOperationResult ConfigurePtp(bool)
    {
        return {false, "PTP is not supported by this camera adapter"};
    }
    virtual DeviceOperationResult ForceIp(const std::string&, const std::string&,
        const std::string&, const std::string&)
    {
        return {false, "GigE ForceIP is not supported by this camera adapter"};
    }
};

class IPlcAdapter : public IDeviceAdapter
{
public:
    virtual DeviceOperationResult ReadTag(const std::string& tag, DeviceValue& value) = 0;
    virtual DeviceOperationResult WriteTag(const std::string& tag, const DeviceValue& value) = 0;
};

class IModbusTcpAdapter : public IDeviceAdapter
{
public:
    virtual DeviceOperationResult ReadCoils(std::uint16_t address, std::uint16_t count,
        std::vector<bool>& values) = 0;
    virtual DeviceOperationResult WriteCoil(std::uint16_t address, bool value) = 0;
    virtual DeviceOperationResult ReadHoldingRegisters(std::uint16_t address,
        std::uint16_t count, std::vector<std::uint16_t>& values) = 0;
    virtual DeviceOperationResult WriteHoldingRegister(std::uint16_t address,
        std::uint16_t value) = 0;
};

class IOpcUaAdapter : public IDeviceAdapter
{
public:
    virtual DeviceOperationResult ReadNode(const std::string& nodeId, DeviceValue& value) = 0;
    virtual DeviceOperationResult WriteNode(const std::string& nodeId,
        const DeviceValue& value) = 0;
};

class ITcpTextAdapter : public IDeviceAdapter
{
public:
    virtual DeviceOperationResult SendText(const std::string& text) = 0;
};

namespace HardwareAdapterService
{
    void SetCamera(std::unique_ptr<ICameraAdapter> camera);
    ICameraAdapter* Camera();
    const ICameraAdapter* CameraReadOnly();
    bool RegisterCamera(const std::string& key,
        std::unique_ptr<ICameraAdapter> camera);
    ICameraAdapter* Camera(const std::string& key);
    const ICameraAdapter* CameraReadOnly(const std::string& key);
    std::vector<std::string> CameraKeys();
    bool RemoveCamera(const std::string& key);

    bool Register(const std::string& key, std::unique_ptr<IDeviceAdapter> adapter);
    IDeviceAdapter* Find(const std::string& key);
    const IDeviceAdapter* FindReadOnly(const std::string& key);
    std::vector<std::string> Keys();
    bool Remove(const std::string& key);

    void DisconnectAll();
    void Clear();
}
