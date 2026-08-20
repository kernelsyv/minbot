#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace minbot::protocol {

using Byte = std::uint8_t;
using Bytes = std::vector<Byte>;

class ProtocolError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

Bytes encode_varint(std::int32_t value);
void append_varint(Bytes& output, std::int32_t value);
std::int32_t read_varint(std::span<const Byte> input, std::size_t& offset);

void append_string(Bytes& output, std::string_view value);
std::string read_string(
    std::span<const Byte> input,
    std::size_t& offset,
    std::size_t maximum_bytes = 1U << 20U);

void append_i64_be(Bytes& output, std::int64_t value);
std::int64_t read_i64_be(std::span<const Byte> input, std::size_t& offset);
void append_i32_be(Bytes& output, std::int32_t value);
std::int32_t read_i32_be(std::span<const Byte> input, std::size_t& offset);
void append_f32_be(Bytes& output, float value);
void append_bool(Bytes& output, bool value);
bool read_bool(std::span<const Byte> input, std::size_t& offset);

Bytes frame_packet(std::span<const Byte> payload);
Bytes make_packet(std::int32_t packet_id, std::span<const Byte> payload = {});
Bytes make_handshake(
    std::int32_t protocol_version,
    std::string_view server_address,
    std::uint16_t server_port);
Bytes make_handshake(
    std::int32_t protocol_version,
    std::string_view server_address,
    std::uint16_t server_port,
    std::int32_t next_state);
Bytes make_status_request();
Bytes make_ping_request(std::int64_t payload);

std::string parse_status_response(std::span<const Byte> packet_body);
std::int64_t parse_pong_response(std::span<const Byte> packet_body);

}  // namespace minbot::protocol
