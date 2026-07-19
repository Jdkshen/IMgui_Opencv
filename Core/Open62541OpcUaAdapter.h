#pragma once

#include "HardwareAdapters.h"

#include <mutex>
#include <string>

struct UA_Client;

class Open62541OpcUaAdapter final : public IOpcUaAdapter
{
public:
    Open62541OpcUaAdapter() = default;
    ~Open62541OpcUaAdapter() override;

    const char* AdapterName() const override;
    DeviceOperationResult Connect(const DeviceEndpoint& endpoint) override;
    void Disconnect() override;
    DeviceConnectionState ConnectionState() const override;
    std::string LastError() const override;

    DeviceOperationResult ReadNode(const std::string& nodeId, DeviceValue& value) override;
    DeviceOperationResult WriteNode(const std::string& nodeId,
        const DeviceValue& value) override;

private:
    DeviceOperationResult Fail(std::string message, bool connectionFault = false);
    void DisconnectLocked(bool clearError);

    mutable std::mutex mutex_;
    UA_Client* client_ = nullptr;
    DeviceConnectionState state_ = DeviceConnectionState::Disconnected;
    std::string lastError_;
    std::string endpointUrl_;
};
