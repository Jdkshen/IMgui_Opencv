#pragma once

#include "HardwareAdapters.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

class ITcpTextTransport
{
public:
    virtual ~ITcpTextTransport() = default;
    virtual DeviceOperationResult Connect(
        const std::string& address, std::uint16_t port, int timeoutMs) = 0;
    virtual void Disconnect() = 0;
    virtual DeviceOperationResult Send(const std::string& text) = 0;
};

class TcpTextAdapter final : public ITcpTextAdapter
{
public:
    explicit TcpTextAdapter(std::unique_ptr<ITcpTextTransport> transport = {});
    ~TcpTextAdapter() override;

    const char* AdapterName() const override;
    DeviceOperationResult Connect(const DeviceEndpoint& endpoint) override;
    void Disconnect() override;
    DeviceConnectionState ConnectionState() const override;
    std::string LastError() const override;
    DeviceOperationResult SendText(const std::string& text) override;

private:
    DeviceOperationResult Fail(std::string message, bool connectionFault = false);

    mutable std::mutex mutex_;
    std::unique_ptr<ITcpTextTransport> transport_;
    DeviceConnectionState state_ = DeviceConnectionState::Disconnected;
    std::string lastError_;
};
