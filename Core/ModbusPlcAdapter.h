#pragma once

#include "HardwareAdapters.h"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>

enum class ModbusPlcTagKind
{
    Coil,
    HoldingRegister
};

enum class ModbusPlcValueType
{
    Boolean,
    UInt16,
    ScaledDouble
};

struct ModbusPlcTagBinding
{
    ModbusPlcTagKind kind = ModbusPlcTagKind::Coil;
    ModbusPlcValueType valueType = ModbusPlcValueType::Boolean;
    std::uint16_t address = 0;
    double scale = 1.0;
    double offset = 0.0;
};

class ModbusPlcAdapter final : public IPlcAdapter
{
public:
    explicit ModbusPlcAdapter(std::unique_ptr<IModbusTcpAdapter> modbus = {});
    ~ModbusPlcAdapter() override;

    bool ConfigureTag(std::string tag, const ModbusPlcTagBinding& binding);
    bool RemoveTag(const std::string& tag);
    void ClearTags();

    const char* AdapterName() const override;
    DeviceOperationResult Connect(const DeviceEndpoint& endpoint) override;
    void Disconnect() override;
    DeviceConnectionState ConnectionState() const override;
    std::string LastError() const override;
    DeviceOperationResult ReadTag(const std::string& tag, DeviceValue& value) override;
    DeviceOperationResult WriteTag(const std::string& tag, const DeviceValue& value) override;

private:
    DeviceOperationResult Fail(std::string message);

    mutable std::mutex mutex_;
    std::unique_ptr<IModbusTcpAdapter> modbus_;
    std::map<std::string, ModbusPlcTagBinding> tags_;
    std::string lastError_;
};
