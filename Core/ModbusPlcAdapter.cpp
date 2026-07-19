#include "ModbusPlcAdapter.h"

#include "ModbusTcpAdapter.h"

#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace
{
bool ToBool(const DeviceValue& value, bool& output)
{
    if (const bool* boolean = std::get_if<bool>(&value))
    {
        output = *boolean;
        return true;
    }
    return false;
}

bool ToRegister(const DeviceValue& value,
    const ModbusPlcTagBinding& binding, std::uint16_t& output)
{
    double raw = 0.0;
    if (const bool* boolean = std::get_if<bool>(&value))
        raw = *boolean ? 1.0 : 0.0;
    else if (const std::int64_t* integer = std::get_if<std::int64_t>(&value))
        raw = static_cast<double>(*integer);
    else if (const double* number = std::get_if<double>(&value))
    {
        if (binding.valueType != ModbusPlcValueType::ScaledDouble ||
            std::abs(binding.scale) <= std::numeric_limits<double>::epsilon())
            return false;
        raw = (*number - binding.offset) / binding.scale;
    }
    else
        return false;

    if (!std::isfinite(raw) || raw < 0.0 || raw > 65535.0)
        return false;
    output = static_cast<std::uint16_t>(std::llround(raw));
    return true;
}
}

ModbusPlcAdapter::ModbusPlcAdapter(std::unique_ptr<IModbusTcpAdapter> modbus)
    : modbus_(modbus ? std::move(modbus)
                     : std::make_unique<ModbusTcpAdapter>())
{
}

ModbusPlcAdapter::~ModbusPlcAdapter()
{
    Disconnect();
}

bool ModbusPlcAdapter::ConfigureTag(
    std::string tag, const ModbusPlcTagBinding& binding)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (tag.empty())
        return false;
    if (binding.kind == ModbusPlcTagKind::Coil &&
        binding.valueType != ModbusPlcValueType::Boolean)
        return false;
    if (binding.valueType == ModbusPlcValueType::ScaledDouble &&
        std::abs(binding.scale) <= std::numeric_limits<double>::epsilon())
        return false;
    tags_[std::move(tag)] = binding;
    return true;
}

bool ModbusPlcAdapter::RemoveTag(const std::string& tag)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tags_.erase(tag) != 0;
}

void ModbusPlcAdapter::ClearTags()
{
    std::lock_guard<std::mutex> lock(mutex_);
    tags_.clear();
}

const char* ModbusPlcAdapter::AdapterName() const
{
    return "Modbus PLC";
}

DeviceOperationResult ModbusPlcAdapter::Connect(const DeviceEndpoint& endpoint)
{
    std::lock_guard<std::mutex> lock(mutex_);
    DeviceOperationResult result = modbus_->Connect(endpoint);
    if (!result.success)
        return Fail(std::move(result.message));
    lastError_.clear();
    return {true, "Modbus PLC connected"};
}

void ModbusPlcAdapter::Disconnect()
{
    std::lock_guard<std::mutex> lock(mutex_);
    modbus_->Disconnect();
}

DeviceConnectionState ModbusPlcAdapter::ConnectionState() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return modbus_->ConnectionState();
}

std::string ModbusPlcAdapter::LastError() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_.empty() ? modbus_->LastError() : lastError_;
}

DeviceOperationResult ModbusPlcAdapter::ReadTag(
    const std::string& tag, DeviceValue& value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = tags_.find(tag);
    if (found == tags_.end())
        return Fail("Unknown PLC tag: " + tag);
    const ModbusPlcTagBinding& binding = found->second;

    if (binding.kind == ModbusPlcTagKind::Coil)
    {
        std::vector<bool> coils;
        DeviceOperationResult result = modbus_->ReadCoils(binding.address, 1, coils);
        if (!result.success || coils.size() != 1)
            return Fail(result.success ? "PLC coil response is empty" : std::move(result.message));
        value = coils.front();
        lastError_.clear();
        return {true, "PLC tag read"};
    }

    std::vector<std::uint16_t> registers;
    DeviceOperationResult result = modbus_->ReadHoldingRegisters(
        binding.address, 1, registers);
    if (!result.success || registers.size() != 1)
        return Fail(result.success ? "PLC register response is empty" : std::move(result.message));
    const std::uint16_t raw = registers.front();
    switch (binding.valueType)
    {
    case ModbusPlcValueType::Boolean:
        value = raw != 0;
        break;
    case ModbusPlcValueType::UInt16:
        value = static_cast<std::int64_t>(raw);
        break;
    case ModbusPlcValueType::ScaledDouble:
        value = raw * binding.scale + binding.offset;
        break;
    }
    lastError_.clear();
    return {true, "PLC tag read"};
}

DeviceOperationResult ModbusPlcAdapter::WriteTag(
    const std::string& tag, const DeviceValue& value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = tags_.find(tag);
    if (found == tags_.end())
        return Fail("Unknown PLC tag: " + tag);
    const ModbusPlcTagBinding& binding = found->second;

    if (binding.kind == ModbusPlcTagKind::Coil)
    {
        bool boolean = false;
        if (!ToBool(value, boolean))
            return Fail("PLC coil tag requires a boolean value");
        DeviceOperationResult result = modbus_->WriteCoil(binding.address, boolean);
        if (!result.success)
            return Fail(std::move(result.message));
        lastError_.clear();
        return {true, "PLC tag written"};
    }

    std::uint16_t raw = 0;
    if (!ToRegister(value, binding, raw))
        return Fail("PLC register value is incompatible or out of range");
    DeviceOperationResult result = modbus_->WriteHoldingRegister(binding.address, raw);
    if (!result.success)
        return Fail(std::move(result.message));
    lastError_.clear();
    return {true, "PLC tag written"};
}

DeviceOperationResult ModbusPlcAdapter::Fail(std::string message)
{
    lastError_ = std::move(message);
    return {false, lastError_};
}
