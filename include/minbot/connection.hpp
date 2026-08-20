#pragma once

#include "minbot/protocol.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace minbot {

class Connection {
public:
    Connection(std::string host, std::uint16_t port, int timeout_ms = 30'000);
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&&) noexcept;
    Connection& operator=(Connection&&) noexcept;

    void send_framed(std::span<const protocol::Byte> packet);
    void send_packet(std::span<const protocol::Byte> packet_body);
    protocol::Bytes receive_packet();
    void enable_compression(std::int32_t threshold);
    void close() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace minbot
