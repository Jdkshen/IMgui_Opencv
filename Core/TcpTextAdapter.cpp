#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include "TcpTextAdapter.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace
{
constexpr int kDefaultTimeoutMs = 2000;

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

class WinsockTcpTextTransport final : public ITcpTextTransport
{
public:
    ~WinsockTcpTextTransport() override
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
        const int resolveResult = getaddrinfo(
            address.c_str(), portText.c_str(), &hints, &addresses);
        if (resolveResult != 0)
            return {false, "解析 TCP 地址失败, code=" + std::to_string(resolveResult)};

        int lastError = 0;
        for (addrinfo* current = addresses; current; current = current->ai_next)
        {
            SOCKET candidate = socket(
                current->ai_family, current->ai_socktype, current->ai_protocol);
            if (candidate == INVALID_SOCKET)
            {
                lastError = WSAGetLastError();
                continue;
            }

            const DWORD timeout = static_cast<DWORD>((std::max)(1, timeoutMs));
            setsockopt(candidate, SOL_SOCKET, SO_SNDTIMEO,
                reinterpret_cast<const char*>(&timeout), sizeof(timeout));
            if (connect(candidate, current->ai_addr,
                    static_cast<int>(current->ai_addrlen)) == 0)
            {
                socket_ = candidate;
                break;
            }
            lastError = WSAGetLastError();
            closesocket(candidate);
        }
        freeaddrinfo(addresses);

        if (socket_ == INVALID_SOCKET)
            return {false, SocketError("TCP 连接", lastError)};
        return {true, "TCP 文本连接已建立"};
    }

    void Disconnect() override
    {
        if (socket_ == INVALID_SOCKET)
            return;
        shutdown(socket_, SD_BOTH);
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }

    DeviceOperationResult Send(const std::string& text) override
    {
        if (socket_ == INVALID_SOCKET)
            return {false, "TCP 文本连接未建立"};

        std::size_t sent = 0;
        while (sent < text.size())
        {
            const int chunk = send(socket_, text.data() + sent,
                static_cast<int>((std::min)(text.size() - sent,
                    static_cast<std::size_t>((std::numeric_limits<int>::max)()))), 0);
            if (chunk == SOCKET_ERROR)
                return {false, SocketError("TCP 文本发送")};
            if (chunk == 0)
                return {false, "TCP 文本发送失败: 对端已关闭连接"};
            sent += static_cast<std::size_t>(chunk);
        }
        return {true, "TCP 文本已发送: " + std::to_string(sent) + " bytes"};
    }

private:
    SOCKET socket_ = INVALID_SOCKET;
};
}

TcpTextAdapter::TcpTextAdapter(std::unique_ptr<ITcpTextTransport> transport)
    : transport_(transport ? std::move(transport)
                           : std::make_unique<WinsockTcpTextTransport>())
{
}

TcpTextAdapter::~TcpTextAdapter()
{
    Disconnect();
}

const char* TcpTextAdapter::AdapterName() const
{
    return "TCP 文本";
}

DeviceOperationResult TcpTextAdapter::Connect(const DeviceEndpoint& endpoint)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (endpoint.address.empty())
        return Fail("TCP 文本地址为空");
    if (endpoint.port == 0)
        return Fail("TCP 文本端口必须在 1-65535 范围内");

    state_ = DeviceConnectionState::Connecting;
    DeviceOperationResult result = transport_->Connect(
        endpoint.address, endpoint.port,
        endpoint.timeoutMs > 0 ? endpoint.timeoutMs : kDefaultTimeoutMs);
    if (!result.success)
        return Fail(std::move(result.message), true);

    state_ = DeviceConnectionState::Connected;
    lastError_.clear();
    return {true, "TCP 文本已连接 (" + endpoint.address + ":" +
        std::to_string(endpoint.port) + ")，发送后不等待响应"};
}

void TcpTextAdapter::Disconnect()
{
    std::lock_guard<std::mutex> lock(mutex_);
    transport_->Disconnect();
    state_ = DeviceConnectionState::Disconnected;
}

DeviceConnectionState TcpTextAdapter::ConnectionState() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

std::string TcpTextAdapter::LastError() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

DeviceOperationResult TcpTextAdapter::SendText(const std::string& text)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != DeviceConnectionState::Connected)
        return Fail("TCP 文本连接未建立");
    if (text.empty())
        return Fail("TCP 文本发送内容为空");

    DeviceOperationResult result = transport_->Send(text);
    if (!result.success)
        return Fail(std::move(result.message), true);
    lastError_.clear();
    return result;
}

DeviceOperationResult TcpTextAdapter::Fail(std::string message, bool connectionFault)
{
    lastError_ = std::move(message);
    if (connectionFault)
    {
        transport_->Disconnect();
        state_ = DeviceConnectionState::Fault;
    }
    return {false, lastError_};
}
