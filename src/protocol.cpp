#include "minbot/protocol.hpp"

#include <bit>
#include <limits>

namespace minbot::protocol {
namespace {

void require_finished(std::span<const Byte> input, std::size_t offset) {
    if (offset != input.size()) {
        throw ProtocolError("packet contains unexpected trailing bytes");
    }
}

}  // namespace

Bytes encode_varint(std::int32_t value) {
    Bytes output;
    append_varint(output, value);
    return output;
}

void append_varint(Bytes& output, std::int32_t value) {
    auto remaining = static_cast<std::uint32_t>(value);

    do {
        auto current = static_cast<Byte>(remaining & 0x7FU);
        remaining >>= 7U;

        if (remaining != 0U) {
            current = static_cast<Byte>(current | 0x80U);
        }

        output.push_back(current);
    } while (remaining != 0U);
}

std::int32_t read_varint(std::span<const Byte> input, std::size_t& offset) {
    std::uint32_t result = 0;

    for (unsigned int index = 0; index < 5U; ++index) {
        if (offset >= input.size()) {
            throw ProtocolError("truncated VarInt");
        }

        const auto current = input[offset++];
        if (index == 4U && (current & 0xF0U) != 0U) {
            throw ProtocolError("VarInt exceeds 32 bits");
        }

        result |= static_cast<std::uint32_t>(current & 0x7FU) << (7U * index);
        if ((current & 0x80U) == 0U) {
            return static_cast<std::int32_t>(result);
        }
    }

    throw ProtocolError("VarInt is too long");
}

void append_string(Bytes& output, std::string_view value) {
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        throw ProtocolError("string is too large for a protocol VarInt length");
    }

    append_varint(output, static_cast<std::int32_t>(value.size()));
    output.insert(output.end(), value.begin(), value.end());
}

std::string read_string(
    std::span<const Byte> input,
    std::size_t& offset,
    std::size_t maximum_bytes) {
    const auto signed_length = read_varint(input, offset);
    if (signed_length < 0) {
        throw ProtocolError("string length cannot be negative");
    }

    const auto length = static_cast<std::size_t>(signed_length);
    if (length > maximum_bytes) {
        throw ProtocolError("string exceeds the configured size limit");
    }

    if (length > input.size() - offset) {
        throw ProtocolError("truncated protocol string");
    }

    const auto* begin = reinterpret_cast<const char*>(input.data() + offset);
    std::string value(begin, length);
    offset += length;
    return value;
}

void append_i64_be(Bytes& output, std::int64_t value) {
    const auto raw = static_cast<std::uint64_t>(value);
    for (int shift = 56; shift >= 0; shift -= 8) {
        output.push_back(static_cast<Byte>((raw >> shift) & 0xFFU));
    }
}

std::int64_t read_i64_be(std::span<const Byte> input, std::size_t& offset) {
    constexpr std::size_t width = sizeof(std::int64_t);
    if (input.size() - offset < width) {
        throw ProtocolError("truncated 64-bit integer");
    }

    std::uint64_t raw = 0;
    for (std::size_t index = 0; index < width; ++index) {
        raw = (raw << 8U) | input[offset++];
    }

    return static_cast<std::int64_t>(raw);
}

void append_i32_be(Bytes& output, std::int32_t value) {
    const auto raw = static_cast<std::uint32_t>(value);
    for (int shift = 24; shift >= 0; shift -= 8) {
        output.push_back(static_cast<Byte>((raw >> shift) & 0xFFU));
    }
}

std::int32_t read_i32_be(std::span<const Byte> input, std::size_t& offset) {
    constexpr std::size_t width = sizeof(std::int32_t);
    if (input.size() - offset < width) {
        throw ProtocolError("truncated 32-bit integer");
    }

    std::uint32_t raw = 0;
    for (std::size_t index = 0; index < width; ++index) {
        raw = (raw << 8U) | input[offset++];
    }
    return static_cast<std::int32_t>(raw);
}

void append_f32_be(Bytes& output, float value) {
    const auto raw = std::bit_cast<std::uint32_t>(value);
    for (int shift = 24; shift >= 0; shift -= 8) {
        output.push_back(static_cast<Byte>((raw >> shift) & 0xFFU));
    }
}

void append_bool(Bytes& output, bool value) {
    output.push_back(value ? 1U : 0U);
}

bool read_bool(std::span<const Byte> input, std::size_t& offset) {
    if (offset >= input.size()) {
        throw ProtocolError("truncated boolean");
    }
    return input[offset++] != 0U;
}

Bytes frame_packet(std::span<const Byte> payload) {
    if (payload.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        throw ProtocolError("packet payload is too large");
    }

    Bytes packet;
    packet.reserve(payload.size() + 5U);
    append_varint(packet, static_cast<std::int32_t>(payload.size()));
    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
}

Bytes make_packet(std::int32_t packet_id, std::span<const Byte> payload) {
    Bytes body;
    append_varint(body, packet_id);
    body.insert(body.end(), payload.begin(), payload.end());
    return frame_packet(body);
}

Bytes make_handshake(
    std::int32_t protocol_version,
    std::string_view server_address,
    std::uint16_t server_port) {
    return make_handshake(protocol_version, server_address, server_port, 1);
}

Bytes make_handshake(
    std::int32_t protocol_version,
    std::string_view server_address,
    std::uint16_t server_port,
    std::int32_t next_state) {
    if (server_address.empty()) {
        throw ProtocolError("server address cannot be empty");
    }
    if (server_address.size() > 255U) {
        throw ProtocolError("server address exceeds 255 bytes");
    }

    Bytes payload;
    append_varint(payload, 0x00);
    append_varint(payload, protocol_version);
    append_string(payload, server_address);
    payload.push_back(static_cast<Byte>((server_port >> 8U) & 0xFFU));
    payload.push_back(static_cast<Byte>(server_port & 0xFFU));
    append_varint(payload, next_state);
    return frame_packet(payload);
}

Bytes make_status_request() {
    const Bytes payload{0x00};
    return frame_packet(payload);
}

Bytes make_ping_request(std::int64_t payload_value) {
    Bytes payload;
    append_varint(payload, 0x01);
    append_i64_be(payload, payload_value);
    return frame_packet(payload);
}

std::string parse_status_response(std::span<const Byte> packet_body) {
    std::size_t offset = 0;
    const auto packet_id = read_varint(packet_body, offset);
    if (packet_id != 0x00) {
        throw ProtocolError("expected a status response packet");
    }

    auto json = read_string(packet_body, offset, 4U << 20U);
    require_finished(packet_body, offset);
    return json;
}

std::int64_t parse_pong_response(std::span<const Byte> packet_body) {
    std::size_t offset = 0;
    const auto packet_id = read_varint(packet_body, offset);
    if (packet_id != 0x01) {
        throw ProtocolError("expected a pong response packet");
    }

    const auto payload = read_i64_be(packet_body, offset);
    require_finished(packet_body, offset);
    return payload;
}

}  // namespace minbot::protocol
