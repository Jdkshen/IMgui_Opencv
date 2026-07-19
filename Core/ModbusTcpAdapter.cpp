#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include "ModbusTcpAdapter.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <utility>

namespace
{
constexpr std::uint16_t kDefaultPort = 502;
constexpr int kDefaultTimeoutMs = 2000;

std::uint16_t ReadU16(const std::uint8_t* data)
{
    return static_cast<std::uint16_t>((data[0] << 8) | data[1]);
}

void AppendU16(std::vector<std::uint8_t>& data, std::uint16_t value)
{
    data.push_back(static_cast<std::uint8_t>(value >> 8));
    data.push_back(static_cast<std::uint8_t>(value & 0xff));
}

std::string SocketError(const char* operation, int errorCode = WSAGetLastError())
{
    if (errorCode == WSAETIMEDOUT)
        return std::string(operation) + "超时 (WSA=10060)";
    if (errorCode == WSAECONNRESET)
        return std::string(operation) + "失败: 连接被设备重置 (WSA=10054)";
    if (errorCode == WSAECONNABORTED)
        return std::string(operation) + "失败: 连接已中止 (WSA=10053)";
    return std::string(operation) + "失败, WSA=" + std::to_string(errorCode);
}

bool EnsureWinsock(std::string& error)
{
    static const int startupResult = []
    {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data);
    }();
    if (startupResult != 0)
    {
        error = "WSAStartup failed, code=" + std::to_string(startupResult);
        return false;
    }
    return true;
}

DeviceOperationResult SendAll(SOCKET socket, const std::uint8_t* data, std::size_t size)
{
    std::size_t sent = 0;
    while (sent < size)
    {
        const int chunk = send(socket,
            reinterpret_cast<const char*>(data + sent),
            static_cast<int>((std::min)(size - sent,
                static_cast<std::size_t>((std::numeric_limits<int>::max)()))), 0);
        if (chunk == SOCKET_ERROR)
            return {false, SocketError("发送请求")};
        if (chunk == 0)
            return {false, "发送请求失败: 设备已关闭连接"};
        sent += static_cast<std::size_t>(chunk);
    }
    return {true, {}};
}

DeviceOperationResult ReceiveAll(SOCKET socket, std::uint8_t* data, std::size_t size,
    const char* operation)
{
    std::size_t received = 0;
    while (received < size)
    {
        const int chunk = recv(socket,
            reinterpret_cast<char*>(data + received),
            static_cast<int>((std::min)(size - received,
                static_cast<std::size_t>((std::numeric_limits<int>::max)()))), 0);
        if (chunk == SOCKET_ERROR)
            return {false, SocketError(operation)};
        if (chunk == 0)
            return {false, std::string(operation) + "失败: 设备已关闭连接"};
        received += static_cast<std::size_t>(chunk);
    }
    return {true, {}};
}

class WinsockModbusTransport final : public IModbusTcpTransport
{
public:
    ~WinsockModbusTransport() override
    {
        Disconnect();
    }

    DeviceOperationResult Connect(
        const std::string& address, std::uint16_t port, int timeoutMs) override
    {
        Disconnect();
        std::string error;
        if (!EnsureWinsock(error))
            return {false, error};

        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        addrinfo* addresses = nullptr;
        const std::string portText = std::to_string(port);
        if (getaddrinfo(address.c_str(), portText.c_str(), &hints, &addresses) != 0)
            return {false, SocketError("getaddrinfo")};

        for (addrinfo* current = addresses; current; current = current->ai_next)
        {
            SOCKET candidate = socket(
                current->ai_family, current->ai_socktype, current->ai_protocol);
            if (candidate == INVALID_SOCKET)
                continue;

            const DWORD timeout = static_cast<DWORD>((std::max)(1, timeoutMs));
            setsockopt(candidate, SOL_SOCKET, SO_RCVTIMEO,
                reinterpret_cast<const char*>(&timeout), sizeof(timeout));
            setsockopt(candidate, SOL_SOCKET, SO_SNDTIMEO,
                reinterpret_cast<const char*>(&timeout), sizeof(timeout));
            if (connect(candidate, current->ai_addr,
                    static_cast<int>(current->ai_addrlen)) == 0)
            {
                socket_ = candidate;
                break;
            }
            closesocket(candidate);
        }
        freeaddrinfo(addresses);

        if (socket_ == INVALID_SOCKET)
            return {false, SocketError("connect")};
        return {true, "Modbus TCP connected"};
    }

    void Disconnect() override
    {
        if (socket_ == INVALID_SOCKET)
            return;
        shutdown(socket_, SD_BOTH);
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }

    DeviceOperationResult Exchange(const std::vector<std::uint8_t>& request,
        std::vector<std::uint8_t>& response) override
    {
        response.clear();
        if (socket_ == INVALID_SOCKET)
            return {false, "Modbus TCP socket is not connected"};
        DeviceOperationResult transfer = SendAll(socket_, request.data(), request.size());
        if (!transfer.success)
            return transfer;

        std::array<std::uint8_t, 7> header{};
        transfer = ReceiveAll(socket_, header.data(), header.size(), "接收响应头");
        if (!transfer.success)
            return transfer;
        const std::uint16_t length = ReadU16(header.data() + 4);
        if (length < 2 || length > 254)
            return {false, "Invalid Modbus TCP MBAP length"};

        response.assign(header.begin(), header.end());
        response.resize(6 + length);
        transfer = ReceiveAll(socket_, response.data() + 7, length - 1, "接收响应体");
        if (!transfer.success)
        {
            response.clear();
            return transfer;
        }
        return {true, "Modbus TCP response received"};
    }

private:
    SOCKET socket_ = INVALID_SOCKET;
};

bool TryParseUnitId(const std::string& resource, std::uint8_t& unitId)
{
    if (resource.empty())
    {
        unitId = 1;
        return true;
    }
    unsigned int value = 0;
    const char* begin = resource.data();
    const char* end = begin + resource.size();
    const auto parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end || value > 255)
        return false;
    unitId = static_cast<std::uint8_t>(value);
    return true;
}

std::string RequestContext(std::uint8_t function, std::uint8_t unitId,
    const std::vector<std::uint8_t>& payload)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string context = "Modbus TCP 请求失败 (FC=";
    context.push_back(kHex[(function >> 4) & 0x0f]);
    context.push_back(kHex[function & 0x0f]);
    context += ", UnitId=" + std::to_string(unitId);
    if (payload.size() >= 2)
        context += ", 地址=" + std::to_string(ReadU16(payload.data()));
    context += ")";
    return context;
}

const char* ModbusExceptionName(std::uint8_t code)
{
    switch (code)
    {
    case 1: return "illegal function";
    case 2: return "illegal data address";
    case 3: return "illegal data value";
    case 4: return "server device failure";
    case 5: return "acknowledge";
    case 6: return "server device busy";
    case 10: return "gateway path unavailable";
    case 11: return "gateway target failed to respond";
    default: return "unknown exception";
    }
}
}

ModbusTcpAdapter::ModbusTcpAdapter(std::unique_ptr<IModbusTcpTransport> transport)
    : transport_(transport ? std::move(transport)
                           : std::make_unique<WinsockModbusTransport>())
{
}

ModbusTcpAdapter::~ModbusTcpAdapter()
{
    Disconnect();
}

const char* ModbusTcpAdapter::AdapterName() const
{
    return "Modbus TCP";
}

DeviceOperationResult ModbusTcpAdapter::Connect(const DeviceEndpoint& endpoint)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (endpoint.address.empty())
        return Fail("Modbus TCP address is empty");

    std::uint8_t unitId = 1;
    if (!TryParseUnitId(endpoint.resource, unitId))
        return Fail("Modbus Unit ID 无效，必须是 0-255 的整数");

    state_ = DeviceConnectionState::Connecting;
    unitId_ = unitId;
    transactionId_ = 0;
    DeviceOperationResult result = transport_->Connect(
        endpoint.address,
        endpoint.port == 0 ? kDefaultPort : endpoint.port,
        endpoint.timeoutMs > 0 ? endpoint.timeoutMs : kDefaultTimeoutMs);
    if (!result.success)
        return Fail(std::move(result.message), true);

    state_ = DeviceConnectionState::Connected;
    lastError_.clear();
    const std::uint16_t port = endpoint.port == 0 ? kDefaultPort : endpoint.port;
    return {true, "Modbus TCP 已建立连接 (" + endpoint.address + ":" +
        std::to_string(port) + ", UnitId=" + std::to_string(unitId_) +
        ")，协议读写尚未验证"};
}

void ModbusTcpAdapter::Disconnect()
{
    std::lock_guard<std::mutex> lock(mutex_);
    transport_->Disconnect();
    state_ = DeviceConnectionState::Disconnected;
}

DeviceConnectionState ModbusTcpAdapter::ConnectionState() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

std::string ModbusTcpAdapter::LastError() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

DeviceOperationResult ModbusTcpAdapter::Fail(
    std::string message, bool connectionFault)
{
    lastError_ = std::move(message);
    if (connectionFault)
    {
        transport_->Disconnect();
        state_ = DeviceConnectionState::Fault;
    }
    return {false, lastError_};
}

DeviceOperationResult ModbusTcpAdapter::Execute(std::uint8_t function,
    const std::vector<std::uint8_t>& payload,
    std::vector<std::uint8_t>& responsePdu)
{
    if (state_ != DeviceConnectionState::Connected)
        return Fail("Modbus TCP is not connected");

    const std::uint16_t transaction = ++transactionId_;
    std::vector<std::uint8_t> request;
    request.reserve(8 + payload.size());
    AppendU16(request, transaction);
    AppendU16(request, 0);
    AppendU16(request, static_cast<std::uint16_t>(2 + payload.size()));
    request.push_back(unitId_);
    request.push_back(function);
    request.insert(request.end(), payload.begin(), payload.end());

    std::vector<std::uint8_t> response;
    DeviceOperationResult exchange = transport_->Exchange(request, response);
    if (!exchange.success)
    {
        std::string message = RequestContext(function, unitId_, payload) + ": " +
            exchange.message;
        if (exchange.message.find("WSA=10060") != std::string::npos)
        {
            message += "；TCP 已连接但设备未返回 Modbus 应答，请检查 Modbus Server、端口、"
                "Unit ID、协议地址及功能码支持";
        }
        return Fail(std::move(message), true);
    }
    if (response.size() < 9)
        return Fail("Modbus TCP response is too short", true);

    const std::uint16_t responseTransaction = ReadU16(response.data());
    const std::uint16_t protocol = ReadU16(response.data() + 2);
    const std::uint16_t length = ReadU16(response.data() + 4);
    if (responseTransaction != transaction || protocol != 0 ||
        length != response.size() - 6 || response[6] != unitId_)
        return Fail("Modbus TCP MBAP validation failed", true);

    responsePdu.assign(response.begin() + 7, response.end());
    if (responsePdu.empty())
        return Fail("Modbus TCP response PDU is empty", true);
    if (responsePdu[0] == static_cast<std::uint8_t>(function | 0x80))
    {
        const std::uint8_t exception = responsePdu.size() > 1 ? responsePdu[1] : 0;
        return Fail("Modbus exception " + std::to_string(exception) + ": " +
            ModbusExceptionName(exception));
    }
    if (responsePdu[0] != function)
        return Fail("Modbus TCP function code mismatch", true);
    return {true, "Modbus TCP operation completed"};
}

DeviceOperationResult ModbusTcpAdapter::ReadCoils(std::uint16_t address,
    std::uint16_t count, std::vector<bool>& values)
{
    std::lock_guard<std::mutex> lock(mutex_);
    values.clear();
    if (count == 0 || count > 2000)
        return Fail("ReadCoils count must be in [1, 2000]");

    std::vector<std::uint8_t> payload;
    AppendU16(payload, address);
    AppendU16(payload, count);
    std::vector<std::uint8_t> response;
    DeviceOperationResult result = Execute(1, payload, response);
    if (!result.success)
        return result;
    const std::size_t expectedBytes = (count + 7) / 8;
    if (response.size() != expectedBytes + 2 || response[1] != expectedBytes)
        return Fail("ReadCoils byte count mismatch", true);

    values.reserve(count);
    for (std::uint16_t index = 0; index < count; ++index)
        values.push_back((response[2 + index / 8] & (1u << (index % 8))) != 0);
    return result;
}

DeviceOperationResult ModbusTcpAdapter::WriteCoil(std::uint16_t address, bool value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::uint8_t> payload;
    AppendU16(payload, address);
    AppendU16(payload, value ? 0xff00 : 0x0000);
    std::vector<std::uint8_t> response;
    DeviceOperationResult result = Execute(5, payload, response);
    if (!result.success)
        return result;
    if (response.size() != 5 ||
        !std::equal(payload.begin(), payload.end(), response.begin() + 1))
        return Fail("WriteCoil echo mismatch", true);
    return result;
}

DeviceOperationResult ModbusTcpAdapter::ReadHoldingRegisters(
    std::uint16_t address, std::uint16_t count,
    std::vector<std::uint16_t>& values)
{
    std::lock_guard<std::mutex> lock(mutex_);
    values.clear();
    if (count == 0 || count > 125)
        return Fail("ReadHoldingRegisters count must be in [1, 125]");

    std::vector<std::uint8_t> payload;
    AppendU16(payload, address);
    AppendU16(payload, count);
    std::vector<std::uint8_t> response;
    DeviceOperationResult result = Execute(3, payload, response);
    if (!result.success)
        return result;
    const std::size_t expectedBytes = static_cast<std::size_t>(count) * 2;
    if (response.size() != expectedBytes + 2 || response[1] != expectedBytes)
        return Fail("ReadHoldingRegisters byte count mismatch", true);

    values.reserve(count);
    for (std::uint16_t index = 0; index < count; ++index)
        values.push_back(ReadU16(response.data() + 2 + index * 2));
    return result;
}

DeviceOperationResult ModbusTcpAdapter::WriteHoldingRegister(
    std::uint16_t address, std::uint16_t value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::uint8_t> payload;
    AppendU16(payload, address);
    AppendU16(payload, value);
    std::vector<std::uint8_t> response;
    DeviceOperationResult result = Execute(6, payload, response);
    if (!result.success)
        return result;
    if (response.size() != 5 ||
        !std::equal(payload.begin(), payload.end(), response.begin() + 1))
        return Fail("WriteHoldingRegister echo mismatch", true);
    return result;
}
