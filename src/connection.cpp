#include "minbot/connection.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

#include <zlib.h>

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

constexpr std::size_t maximum_packet_size = 16U << 20U;
constexpr std::size_t maximum_wire_packet_size = 17U << 20U;

const std::string& require_host(const std::string& host) {
    if (host.empty()) {
        throw std::invalid_argument("host cannot be empty");
    }
    return host;
}

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

void close_native_socket(NativeSocket socket) noexcept {
    if (socket == invalid_socket) {
        return;
    }
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

void shutdown_native_socket(NativeSocket socket) noexcept {
    if (socket == invalid_socket) {
        return;
    }
#ifdef _WIN32
    shutdown(socket, SD_BOTH);
#else
    shutdown(socket, SHUT_RDWR);
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

NativeSocket connect_tcp(const std::string& host, std::uint16_t port, int timeout_ms) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* raw_addresses = nullptr;
    const auto port_string = std::to_string(port);
    const int result = getaddrinfo(host.c_str(), port_string.c_str(), &hints, &raw_addresses);
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
        const auto candidate = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (candidate == invalid_socket) {
            last_error = socket_error("socket");
            continue;
        }

        try {
            set_socket_timeout(candidate, timeout_ms);
            if (::connect(candidate, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0) {
                return candidate;
            }
            last_error = socket_error("connect");
        } catch (const std::exception& error) {
            last_error = error.what();
        }
        close_native_socket(candidate);
    }

    throw std::runtime_error("could not connect to " + host + ':' + port_string + ": " + last_error);
}

void send_all(NativeSocket socket, std::span<const protocol::Byte> data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const auto chunk_size = static_cast<int>((std::min)(
            data.size() - sent,
            static_cast<std::size_t>(std::numeric_limits<int>::max())));
#ifdef _WIN32
        const int result = ::send(socket, reinterpret_cast<const char*>(data.data() + sent), chunk_size, 0);
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
        const auto chunk_size = static_cast<int>((std::min)(
            output.size() - received,
            static_cast<std::size_t>(std::numeric_limits<int>::max())));
#ifdef _WIN32
        const int result = ::recv(socket, reinterpret_cast<char*>(output.data() + received), chunk_size, 0);
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

std::int32_t receive_varint(NativeSocket socket) {
    std::uint32_t result = 0;
    for (unsigned int index = 0; index < 5U; ++index) {
        protocol::Byte current = 0;
        receive_exact(socket, std::span<protocol::Byte>(&current, 1));
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

protocol::Bytes compress_packet_body(
    std::span<const protocol::Byte> packet_body,
    std::int32_t threshold) {
    if (packet_body.size() > maximum_packet_size) {
        throw protocol::ProtocolError("packet exceeds the 16 MiB safety limit");
    }

    protocol::Bytes compressed_body;
    if (packet_body.size() < static_cast<std::size_t>(threshold)) {
        protocol::append_varint(compressed_body, 0);
        compressed_body.insert(compressed_body.end(), packet_body.begin(), packet_body.end());
        return protocol::frame_packet(compressed_body);
    }

    protocol::append_varint(compressed_body, static_cast<std::int32_t>(packet_body.size()));
    const auto prefix_size = compressed_body.size();
    auto capacity = compressBound(static_cast<uLong>(packet_body.size()));
    compressed_body.resize(prefix_size + static_cast<std::size_t>(capacity));
    const int result = compress2(
        compressed_body.data() + prefix_size,
        &capacity,
        packet_body.data(),
        static_cast<uLong>(packet_body.size()),
        Z_DEFAULT_COMPRESSION);
    if (result != Z_OK) {
        throw protocol::ProtocolError("zlib could not compress packet: error " + std::to_string(result));
    }
    compressed_body.resize(prefix_size + static_cast<std::size_t>(capacity));
    return protocol::frame_packet(compressed_body);
}

protocol::Bytes decompress_packet_body(
    std::span<const protocol::Byte> wire_body,
    std::int32_t threshold) {
    std::size_t offset = 0;
    const auto signed_length = protocol::read_varint(wire_body, offset);
    if (signed_length < 0) {
        throw protocol::ProtocolError("uncompressed packet length cannot be negative");
    }

    if (signed_length == 0) {
        if (offset >= wire_body.size()) {
            throw protocol::ProtocolError("compressed packet body cannot be empty");
        }
        const auto raw_size = wire_body.size() - offset;
        if (raw_size >= static_cast<std::size_t>(threshold)) {
            throw protocol::ProtocolError("uncompressed packet reaches the negotiated threshold");
        }
        return protocol::Bytes(
            wire_body.begin() + static_cast<std::ptrdiff_t>(offset),
            wire_body.end());
    }

    const auto uncompressed_length = static_cast<std::size_t>(signed_length);
    if (uncompressed_length < static_cast<std::size_t>(threshold)) {
        throw protocol::ProtocolError("compressed packet is smaller than the negotiated threshold");
    }
    if (uncompressed_length > maximum_packet_size) {
        throw protocol::ProtocolError("uncompressed packet exceeds the 16 MiB safety limit");
    }
    if (offset >= wire_body.size()) {
        throw protocol::ProtocolError("compressed packet has no zlib data");
    }

    protocol::Bytes packet(uncompressed_length);
    auto output_length = static_cast<uLong>(packet.size());
    const int result = uncompress(
        packet.data(),
        &output_length,
        wire_body.data() + offset,
        static_cast<uLong>(wire_body.size() - offset));
    if (result != Z_OK || output_length != packet.size()) {
        throw protocol::ProtocolError("zlib could not decompress packet: error " + std::to_string(result));
    }
    return packet;
}

}  // namespace

class Connection::Impl {
public:
    Impl(const std::string& host, std::uint16_t port, int timeout_ms)
        : socket(connect_tcp(host, port, timeout_ms)) {}

    ~Impl() {
        close();
    }

    void close() noexcept {
        const auto current = socket.exchange(invalid_socket);
        if (current != invalid_socket) {
            shutdown_native_socket(current);
            close_native_socket(current);
        }
    }

    SocketRuntime runtime;
    std::atomic<NativeSocket> socket = invalid_socket;
    std::atomic<std::int32_t> compression_threshold = -1;
    std::mutex send_mutex;
};

Connection::Connection(std::string host, std::uint16_t port, int timeout_ms)
    : impl_(std::make_unique<Impl>(require_host(host), port, timeout_ms)) {}

Connection::~Connection() = default;
Connection::Connection(Connection&&) noexcept = default;
Connection& Connection::operator=(Connection&&) noexcept = default;

void Connection::send_framed(std::span<const protocol::Byte> packet) {
    std::scoped_lock lock(impl_->send_mutex);
    send_all(impl_->socket.load(), packet);
}

void Connection::send_packet(std::span<const protocol::Byte> packet_body) {
    const auto threshold = impl_->compression_threshold.load();
    if (threshold < 0) {
        send_framed(protocol::frame_packet(packet_body));
    } else {
        send_framed(compress_packet_body(packet_body, threshold));
    }
}

protocol::Bytes Connection::receive_packet() {
    const auto socket = impl_->socket.load();
    const auto signed_length = receive_varint(socket);
    if (signed_length <= 0) {
        throw protocol::ProtocolError("packet body cannot be empty");
    }

    const auto length = static_cast<std::size_t>(signed_length);
    if (length > maximum_wire_packet_size) {
        throw protocol::ProtocolError("wire packet exceeds the 17 MiB safety limit");
    }

    protocol::Bytes wire_body(length);
    receive_exact(socket, wire_body);
    const auto threshold = impl_->compression_threshold.load();
    if (threshold < 0) {
        if (wire_body.size() > maximum_packet_size) {
            throw protocol::ProtocolError("packet exceeds the 16 MiB safety limit");
        }
        return wire_body;
    }
    return decompress_packet_body(wire_body, threshold);
}

void Connection::enable_compression(std::int32_t threshold) {
    if (threshold < 0) {
        throw std::invalid_argument("compression threshold cannot be negative");
    }
    impl_->compression_threshold.store(threshold);
}

void Connection::close() noexcept {
    if (impl_) {
        impl_->close();
    }
}

}  // namespace minbot
