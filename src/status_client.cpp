#include "minbot/status_client.hpp"

#include "minbot/protocol.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace minbot {
namespace {

constexpr std::size_t maximum_packet_size = 4U << 20U;

#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket invalid_socket = INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket invalid_socket = -1;
#endif

std::string socket_error(const std::string& operation) {
#ifdef _WIN32
    return operation + " failed with WinSock error " + std::to_string(WSAGetLastError());
#else
    return operation + " failed: " + std::strerror(errno);
#endif
}

void close_socket(NativeSocket socket) noexcept {
    if (socket == invalid_socket) {
        return;
    }
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

class SocketRuntime {
public:
    SocketRuntime() {
#ifdef _WIN32
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw std::runtime_error("failed to initialize WinSock");
        }
#endif
    }

    ~SocketRuntime() {
#ifdef _WIN32
        WSACleanup();
#endif
    }

    SocketRuntime(const SocketRuntime&) = delete;
    SocketRuntime& operator=(const SocketRuntime&) = delete;
};

class SocketHandle {
public:
    SocketHandle() = default;
    explicit SocketHandle(NativeSocket socket) : socket_(socket) {}

    ~SocketHandle() {
        close_socket(socket_);
    }

    SocketHandle(const SocketHandle&) = delete;
    SocketHandle& operator=(const SocketHandle&) = delete;

    SocketHandle(SocketHandle&& other) noexcept : socket_(std::exchange(other.socket_, invalid_socket)) {}

    SocketHandle& operator=(SocketHandle&& other) noexcept {
        if (this != &other) {
            close_socket(socket_);
            socket_ = std::exchange(other.socket_, invalid_socket);
        }
        return *this;
    }

    NativeSocket get() const noexcept {
        return socket_;
    }

    explicit operator bool() const noexcept {
        return socket_ != invalid_socket;
    }

private:
    NativeSocket socket_ = invalid_socket;
};

struct AddressInfoDeleter {
    void operator()(addrinfo* address) const noexcept {
        if (address != nullptr) {
            freeaddrinfo(address);
        }
    }
};

void set_socket_timeout(NativeSocket socket, int timeout_ms) {
    if (timeout_ms <= 0) {
        throw std::invalid_argument("timeout must be greater than zero");
    }

#ifdef _WIN32
    const auto timeout = static_cast<DWORD>(timeout_ms);
    if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) != 0 ||
        setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) != 0) {
        throw std::runtime_error(socket_error("setsockopt"));
    }
#else
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
        setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0) {
        throw std::runtime_error(socket_error("setsockopt"));
    }
#endif
}

SocketHandle connect_tcp(const StatusOptions& options) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* raw_addresses = nullptr;
    const auto port = std::to_string(options.port);
    const int result = getaddrinfo(options.host.c_str(), port.c_str(), &hints, &raw_addresses);
    if (result != 0) {
#ifdef _WIN32
        throw std::runtime_error("DNS lookup failed with WinSock error " + std::to_string(result));
#else
        throw std::runtime_error(std::string("DNS lookup failed: ") + gai_strerror(result));
#endif
    }

    std::unique_ptr<addrinfo, AddressInfoDeleter> addresses(raw_addresses);
    std::string last_error = "no compatible address was returned";

    for (auto* address = addresses.get(); address != nullptr; address = address->ai_next) {
        SocketHandle candidate(::socket(address->ai_family, address->ai_socktype, address->ai_protocol));
        if (!candidate) {
            last_error = socket_error("socket");
            continue;
        }

        try {
            set_socket_timeout(candidate.get(), options.timeout_ms);
        } catch (const std::exception& error) {
            last_error = error.what();
            continue;
        }

        if (::connect(candidate.get(), address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0) {
            return candidate;
        }

        last_error = socket_error("connect");
    }

    throw std::runtime_error("could not connect to " + options.host + ':' + port + ": " + last_error);
}

void send_all(NativeSocket socket, std::span<const protocol::Byte> data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const auto remaining = data.size() - sent;
        const auto chunk_size = static_cast<int>((std::min)(
            remaining,
            static_cast<std::size_t>(std::numeric_limits<int>::max())));

#ifdef _WIN32
        const int result = ::send(
            socket,
            reinterpret_cast<const char*>(data.data() + sent),
            chunk_size,
            0);
#else
        const int result = static_cast<int>(::send(socket, data.data() + sent, chunk_size, 0));
#endif
        if (result <= 0) {
            throw std::runtime_error(socket_error("send"));
        }
        sent += static_cast<std::size_t>(result);
    }
}

void receive_exact(NativeSocket socket, std::span<protocol::Byte> output) {
    std::size_t received = 0;
    while (received < output.size()) {
        const auto remaining = output.size() - received;
        const auto chunk_size = static_cast<int>((std::min)(
            remaining,
            static_cast<std::size_t>(std::numeric_limits<int>::max())));

#ifdef _WIN32
        const int result = ::recv(
            socket,
            reinterpret_cast<char*>(output.data() + received),
            chunk_size,
            0);
#else
        const int result = static_cast<int>(::recv(socket, output.data() + received, chunk_size, 0));
#endif
        if (result == 0) {
            throw std::runtime_error("connection closed while receiving a packet");
        }
        if (result < 0) {
            throw std::runtime_error(socket_error("receive"));
        }
        received += static_cast<std::size_t>(result);
    }
}

protocol::Byte receive_byte(NativeSocket socket) {
    protocol::Byte value = 0;
    receive_exact(socket, std::span<protocol::Byte>(&value, 1));
    return value;
}

std::int32_t receive_varint(NativeSocket socket) {
    std::uint32_t result = 0;

    for (unsigned int index = 0; index < 5U; ++index) {
        const auto current = receive_byte(socket);
        if (index == 4U && (current & 0xF0U) != 0U) {
            throw protocol::ProtocolError("packet length VarInt exceeds 32 bits");
        }

        result |= static_cast<std::uint32_t>(current & 0x7FU) << (7U * index);
        if ((current & 0x80U) == 0U) {
            return static_cast<std::int32_t>(result);
        }
    }

    throw protocol::ProtocolError("packet length VarInt is too long");
}

protocol::Bytes receive_packet(NativeSocket socket) {
    const auto signed_length = receive_varint(socket);
    if (signed_length < 0) {
        throw protocol::ProtocolError("packet length cannot be negative");
    }

    const auto length = static_cast<std::size_t>(signed_length);
    if (length == 0U) {
        throw protocol::ProtocolError("packet body cannot be empty");
    }
    if (length > maximum_packet_size) {
        throw protocol::ProtocolError("packet exceeds the 4 MiB safety limit");
    }

    protocol::Bytes packet(length);
    receive_exact(socket, packet);
    return packet;
}

std::int64_t unix_time_milliseconds() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

}  // namespace

StatusResult query_server_status(const StatusOptions& options) {
    if (options.host.empty()) {
        throw std::invalid_argument("host cannot be empty");
    }

    SocketRuntime runtime;
    auto socket = connect_tcp(options);

    const auto handshake = protocol::make_handshake(
        options.protocol_version,
        options.host,
        options.port);
    send_all(socket.get(), handshake);

    const auto status_request = protocol::make_status_request();
    send_all(socket.get(), status_request);

    auto result = StatusResult{};
    result.json = protocol::parse_status_response(receive_packet(socket.get()));

    const auto ping_payload = unix_time_milliseconds();
    const auto ping_request = protocol::make_ping_request(ping_payload);
    const auto started = std::chrono::steady_clock::now();
    send_all(socket.get(), ping_request);

    const auto echoed_payload = protocol::parse_pong_response(receive_packet(socket.get()));
    const auto finished = std::chrono::steady_clock::now();
    if (echoed_payload != ping_payload) {
        throw protocol::ProtocolError("server returned a mismatched pong payload");
    }

    result.latency_ms = std::chrono::duration<double, std::milli>(finished - started).count();
    return result;
}

}  // namespace minbot
