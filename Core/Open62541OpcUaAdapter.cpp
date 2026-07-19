#include "Open62541OpcUaAdapter.h"

#include "../third_party/open62541/open62541.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <type_traits>
#include <utility>

namespace
{
std::string StatusMessage(const char* action, UA_StatusCode status)
{
    std::ostringstream stream;
    stream << action << ": " << UA_StatusCode_name(status)
           << " (0x" << std::hex << std::uppercase << std::setw(8)
           << std::setfill('0') << status << ')';
    return stream.str();
}

bool IsConnectionFault(UA_StatusCode status)
{
    switch (status)
    {
    case UA_STATUSCODE_BADCOMMUNICATIONERROR:
    case UA_STATUSCODE_BADTIMEOUT:
    case UA_STATUSCODE_BADSERVERNOTCONNECTED:
    case UA_STATUSCODE_BADSESSIONCLOSED:
    case UA_STATUSCODE_BADREQUESTTIMEOUT:
    case UA_STATUSCODE_BADSECURECHANNELCLOSED:
    case UA_STATUSCODE_BADNOTCONNECTED:
    case UA_STATUSCODE_BADCONNECTIONCLOSED:
        return true;
    default:
        return false;
    }
}

std::string AppendResource(std::string endpointUrl, const std::string& resource)
{
    if (resource.empty())
        return endpointUrl;
    const bool endpointHasSlash = !endpointUrl.empty() && endpointUrl.back() == '/';
    const bool resourceHasSlash = resource.front() == '/';
    if (endpointHasSlash && resourceHasSlash)
        endpointUrl.pop_back();
    else if (!endpointHasSlash && !resourceHasSlash)
        endpointUrl.push_back('/');
    endpointUrl += resource;
    return endpointUrl;
}

DeviceOperationResult BuildEndpointUrl(const DeviceEndpoint& endpoint, std::string& url)
{
    if (endpoint.address.empty())
        return {false, "OPC UA endpoint address is empty"};

    constexpr const char* scheme = "opc.tcp://";
    if (endpoint.address.rfind(scheme, 0) == 0)
    {
        url = AppendResource(endpoint.address, endpoint.resource);
        return {true, {}};
    }

    std::string host = endpoint.address;
    if (host.find(':') != std::string::npos &&
        !(host.front() == '[' && host.back() == ']'))
    {
        host = '[' + host + ']';
    }
    const std::uint16_t port = endpoint.port == 0 ? 4840 : endpoint.port;
    url = std::string(scheme) + host + ':' + std::to_string(port);
    url = AppendResource(std::move(url), endpoint.resource);
    return {true, {}};
}

UA_StatusCode ParseNodeId(const std::string& text, UA_NodeId& nodeId)
{
    UA_NodeId_init(&nodeId);
    if (text.empty())
        return UA_STATUSCODE_BADNODEIDINVALID;
    UA_String encoded{static_cast<size_t>(text.size()),
        reinterpret_cast<UA_Byte*>(const_cast<char*>(text.data()))};
    return UA_NodeId_parse(&nodeId, encoded);
}

template <typename T>
bool AssignSigned(std::int64_t value, UA_Variant& variant, const UA_DataType& type)
{
    if (value < static_cast<std::int64_t>((std::numeric_limits<T>::min)()) ||
        value > static_cast<std::int64_t>((std::numeric_limits<T>::max)()))
    {
        return false;
    }
    const T converted = static_cast<T>(value);
    return UA_Variant_setScalarCopy(&variant, &converted, &type) == UA_STATUSCODE_GOOD;
}

template <typename T>
bool AssignUnsigned(std::int64_t value, UA_Variant& variant, const UA_DataType& type)
{
    if (value < 0 || static_cast<std::uint64_t>(value) >
        static_cast<std::uint64_t>((std::numeric_limits<T>::max)()))
    {
        return false;
    }
    const T converted = static_cast<T>(value);
    return UA_Variant_setScalarCopy(&variant, &converted, &type) == UA_STATUSCODE_GOOD;
}

UA_StatusCode SetVariantValue(const DeviceValue& value, const UA_DataType* preferredType,
    UA_Variant& variant, std::string& error)
{
    UA_Variant_init(&variant);
    UA_StatusCode status = UA_STATUSCODE_GOOD;

    if (const auto* boolean = std::get_if<bool>(&value))
    {
        const UA_Boolean converted = *boolean ? UA_TRUE : UA_FALSE;
        status = UA_Variant_setScalarCopy(&variant, &converted,
            &UA_TYPES[UA_TYPES_BOOLEAN]);
    }
    else if (const auto* integer = std::get_if<std::int64_t>(&value))
    {
        bool assigned = false;
        if (preferredType == &UA_TYPES[UA_TYPES_SBYTE])
            assigned = AssignSigned<UA_SByte>(*integer, variant, *preferredType);
        else if (preferredType == &UA_TYPES[UA_TYPES_BYTE])
            assigned = AssignUnsigned<UA_Byte>(*integer, variant, *preferredType);
        else if (preferredType == &UA_TYPES[UA_TYPES_INT16])
            assigned = AssignSigned<UA_Int16>(*integer, variant, *preferredType);
        else if (preferredType == &UA_TYPES[UA_TYPES_UINT16])
            assigned = AssignUnsigned<UA_UInt16>(*integer, variant, *preferredType);
        else if (preferredType == &UA_TYPES[UA_TYPES_INT32])
            assigned = AssignSigned<UA_Int32>(*integer, variant, *preferredType);
        else if (preferredType == &UA_TYPES[UA_TYPES_UINT32])
            assigned = AssignUnsigned<UA_UInt32>(*integer, variant, *preferredType);
        else if (preferredType == &UA_TYPES[UA_TYPES_UINT64])
            assigned = AssignUnsigned<UA_UInt64>(*integer, variant, *preferredType);
        else
            assigned = AssignSigned<UA_Int64>(*integer, variant, UA_TYPES[UA_TYPES_INT64]);

        if (!assigned)
        {
            error = "OPC UA integer value is outside the target node range";
            return UA_STATUSCODE_BADOUTOFRANGE;
        }
    }
    else if (const auto* number = std::get_if<double>(&value))
    {
        if (!std::isfinite(*number))
        {
            error = "OPC UA floating-point value must be finite";
            return UA_STATUSCODE_BADOUTOFRANGE;
        }
        if (preferredType == &UA_TYPES[UA_TYPES_FLOAT])
        {
            if (*number < -(std::numeric_limits<UA_Float>::max)() ||
                *number > (std::numeric_limits<UA_Float>::max)())
            {
                error = "OPC UA floating-point value is outside the target node range";
                return UA_STATUSCODE_BADOUTOFRANGE;
            }
            const UA_Float converted = static_cast<UA_Float>(*number);
            status = UA_Variant_setScalarCopy(&variant, &converted,
                &UA_TYPES[UA_TYPES_FLOAT]);
        }
        else
        {
            const UA_Double converted = *number;
            status = UA_Variant_setScalarCopy(&variant, &converted,
                &UA_TYPES[UA_TYPES_DOUBLE]);
        }
    }
    else if (const auto* text = std::get_if<std::string>(&value))
    {
        UA_String converted{static_cast<size_t>(text->size()),
            reinterpret_cast<UA_Byte*>(const_cast<char*>(text->data()))};
        status = UA_Variant_setScalarCopy(&variant, &converted,
            &UA_TYPES[UA_TYPES_STRING]);
    }

    if (status != UA_STATUSCODE_GOOD)
        error = StatusMessage("OPC UA value conversion failed", status);
    return status;
}

DeviceOperationResult ReadDeviceValue(const UA_Variant& variant, DeviceValue& value)
{
    if (!UA_Variant_isScalar(&variant) || !variant.data)
        return {false, "OPC UA node is not a scalar value"};

    if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_BOOLEAN]))
        value = *static_cast<const UA_Boolean*>(variant.data) != UA_FALSE;
    else if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_SBYTE]))
        value = static_cast<std::int64_t>(*static_cast<const UA_SByte*>(variant.data));
    else if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_BYTE]))
        value = static_cast<std::int64_t>(*static_cast<const UA_Byte*>(variant.data));
    else if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_INT16]))
        value = static_cast<std::int64_t>(*static_cast<const UA_Int16*>(variant.data));
    else if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_UINT16]))
        value = static_cast<std::int64_t>(*static_cast<const UA_UInt16*>(variant.data));
    else if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_INT32]))
        value = static_cast<std::int64_t>(*static_cast<const UA_Int32*>(variant.data));
    else if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_UINT32]))
        value = static_cast<std::int64_t>(*static_cast<const UA_UInt32*>(variant.data));
    else if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_INT64]))
        value = static_cast<std::int64_t>(*static_cast<const UA_Int64*>(variant.data));
    else if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_UINT64]))
    {
        const UA_UInt64 number = *static_cast<const UA_UInt64*>(variant.data);
        if (number > static_cast<UA_UInt64>((std::numeric_limits<std::int64_t>::max)()))
            return {false, "OPC UA UInt64 value exceeds DeviceValue range"};
        value = static_cast<std::int64_t>(number);
    }
    else if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_FLOAT]))
        value = static_cast<double>(*static_cast<const UA_Float*>(variant.data));
    else if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_DOUBLE]))
        value = static_cast<double>(*static_cast<const UA_Double*>(variant.data));
    else if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_STRING]))
    {
        const UA_String& text = *static_cast<const UA_String*>(variant.data);
        value = std::string(reinterpret_cast<const char*>(text.data), text.length);
    }
    else
        return {false, "OPC UA node type is not supported by DeviceValue"};

    return {true, {}};
}
}

Open62541OpcUaAdapter::~Open62541OpcUaAdapter()
{
    Disconnect();
}

const char* Open62541OpcUaAdapter::AdapterName() const
{
    return "open62541 OPC UA";
}

DeviceOperationResult Open62541OpcUaAdapter::Connect(const DeviceEndpoint& endpoint)
{
    std::lock_guard<std::mutex> lock(mutex_);
    DisconnectLocked(false);
    state_ = DeviceConnectionState::Connecting;

    std::string endpointUrl;
    const DeviceOperationResult endpointResult = BuildEndpointUrl(endpoint, endpointUrl);
    if (!endpointResult.success)
        return Fail(endpointResult.message, true);

    client_ = UA_Client_new();
    if (!client_)
        return Fail("Failed to allocate OPC UA client", true);

    UA_ClientConfig* config = UA_Client_getConfig(client_);
    UA_StatusCode status = UA_ClientConfig_setDefault(config);
    if (status != UA_STATUSCODE_GOOD)
        return Fail(StatusMessage("OPC UA client configuration failed", status), true);
    config->timeout = static_cast<UA_UInt32>((std::max)(1, endpoint.timeoutMs));

    status = UA_Client_connect(client_, endpointUrl.c_str());
    if (status != UA_STATUSCODE_GOOD)
        return Fail(StatusMessage("OPC UA connection failed", status), true);

    endpointUrl_ = std::move(endpointUrl);
    state_ = DeviceConnectionState::Connected;
    lastError_.clear();
    return {true, {}};
}

void Open62541OpcUaAdapter::Disconnect()
{
    std::lock_guard<std::mutex> lock(mutex_);
    DisconnectLocked(true);
}

DeviceConnectionState Open62541OpcUaAdapter::ConnectionState() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

std::string Open62541OpcUaAdapter::LastError() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

DeviceOperationResult Open62541OpcUaAdapter::ReadNode(
    const std::string& nodeIdText, DeviceValue& value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!client_ || state_ != DeviceConnectionState::Connected)
        return Fail("OPC UA client is not connected");

    UA_NodeId nodeId;
    UA_StatusCode status = ParseNodeId(nodeIdText, nodeId);
    if (status != UA_STATUSCODE_GOOD)
    {
        UA_NodeId_clear(&nodeId);
        return Fail(StatusMessage("Invalid OPC UA NodeId", status));
    }

    UA_Variant variant;
    UA_Variant_init(&variant);
    status = UA_Client_readValueAttribute(client_, nodeId, &variant);
    UA_NodeId_clear(&nodeId);
    if (status != UA_STATUSCODE_GOOD)
    {
        UA_Variant_clear(&variant);
        return Fail(StatusMessage("OPC UA read failed", status), IsConnectionFault(status));
    }

    DeviceOperationResult result = ReadDeviceValue(variant, value);
    UA_Variant_clear(&variant);
    if (!result.success)
        return Fail(result.message);
    lastError_.clear();
    return result;
}

DeviceOperationResult Open62541OpcUaAdapter::WriteNode(
    const std::string& nodeIdText, const DeviceValue& value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!client_ || state_ != DeviceConnectionState::Connected)
        return Fail("OPC UA client is not connected");

    UA_NodeId nodeId;
    UA_StatusCode status = ParseNodeId(nodeIdText, nodeId);
    if (status != UA_STATUSCODE_GOOD)
    {
        UA_NodeId_clear(&nodeId);
        return Fail(StatusMessage("Invalid OPC UA NodeId", status));
    }

    const UA_DataType* preferredType = nullptr;
    UA_Variant current;
    UA_Variant_init(&current);
    const UA_StatusCode readStatus = UA_Client_readValueAttribute(client_, nodeId, &current);
    if (readStatus == UA_STATUSCODE_GOOD && UA_Variant_isScalar(&current))
        preferredType = current.type;
    else if (IsConnectionFault(readStatus))
    {
        UA_Variant_clear(&current);
        UA_NodeId_clear(&nodeId);
        return Fail(StatusMessage("OPC UA type read failed", readStatus), true);
    }

    UA_Variant writeValue;
    std::string conversionError;
    status = SetVariantValue(value, preferredType, writeValue, conversionError);
    UA_Variant_clear(&current);
    if (status != UA_STATUSCODE_GOOD)
    {
        UA_Variant_clear(&writeValue);
        UA_NodeId_clear(&nodeId);
        return Fail(conversionError);
    }

    status = UA_Client_writeValueAttribute(client_, nodeId, &writeValue);
    UA_Variant_clear(&writeValue);
    UA_NodeId_clear(&nodeId);
    if (status != UA_STATUSCODE_GOOD)
        return Fail(StatusMessage("OPC UA write failed", status), IsConnectionFault(status));

    lastError_.clear();
    return {true, {}};
}

DeviceOperationResult Open62541OpcUaAdapter::Fail(
    std::string message, bool connectionFault)
{
    lastError_ = std::move(message);
    if (connectionFault)
    {
        if (client_)
        {
            UA_Client_delete(client_);
            client_ = nullptr;
        }
        endpointUrl_.clear();
        state_ = DeviceConnectionState::Fault;
    }
    return {false, lastError_};
}

void Open62541OpcUaAdapter::DisconnectLocked(bool clearError)
{
    if (client_)
    {
        if (state_ == DeviceConnectionState::Connected)
            UA_Client_disconnect(client_);
        UA_Client_delete(client_);
        client_ = nullptr;
    }
    endpointUrl_.clear();
    state_ = DeviceConnectionState::Disconnected;
    if (clearError)
        lastError_.clear();
}
