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
    virtual DeviceOperationResult StartStream() = 0;
    virtual void StopStream() = 0;
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

    bool Register(const std::string& key, std::unique_ptr<IDeviceAdapter> adapter);
    IDeviceAdapter* Find(const std::string& key);
    const IDeviceAdapter* FindReadOnly(const std::string& key);
    std::vector<std::string> Keys();
    bool Remove(const std::string& key);

    void DisconnectAll();
    void Clear();
}
