#pragma once

#include "HardwareAdapters.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class IModbusTcpTransport
{
public:
    virtual ~IModbusTcpTransport() = default;
    virtual DeviceOperationResult Connect(
        const std::string& address, std::uint16_t port, int timeoutMs) = 0;
    virtual void Disconnect() = 0;
    virtual DeviceOperationResult Exchange(
        const std::vector<std::uint8_t>& request,
        std::vector<std::uint8_t>& response) = 0;
};

class ModbusTcpAdapter final : public IModbusTcpAdapter
{
public:
    explicit ModbusTcpAdapter(std::unique_ptr<IModbusTcpTransport> transport = {});
    ~ModbusTcpAdapter() override;

    const char* AdapterName() const override;
    DeviceOperationResult Connect(const DeviceEndpoint& endpoint) override;
    void Disconnect() override;
    DeviceConnectionState ConnectionState() const override;
    std::string LastError() const override;

    DeviceOperationResult ReadCoils(std::uint16_t address, std::uint16_t count,
        std::vector<bool>& values) override;
    DeviceOperationResult WriteCoil(std::uint16_t address, bool value) override;
    DeviceOperationResult ReadHoldingRegisters(std::uint16_t address,
        std::uint16_t count, std::vector<std::uint16_t>& values) override;
    DeviceOperationResult WriteHoldingRegister(std::uint16_t address,
        std::uint16_t value) override;

private:
    DeviceOperationResult Execute(std::uint8_t function,
        const std::vector<std::uint8_t>& payload,
        std::vector<std::uint8_t>& responsePdu);
    DeviceOperationResult Fail(std::string message,
        bool connectionFault = false);

    mutable std::mutex mutex_;
    std::unique_ptr<IModbusTcpTransport> transport_;
    DeviceConnectionState state_ = DeviceConnectionState::Disconnected;
    std::string lastError_;
    std::uint16_t transactionId_ = 0;
    std::uint8_t unitId_ = 1;
};
