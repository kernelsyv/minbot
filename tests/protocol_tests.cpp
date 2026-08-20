#include "minbot/protocol.hpp"
#include "minbot/version.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void fail(std::string_view test, std::string_view message) {
    ++failures;
    std::cerr << "[FAIL] " << test << ": " << message << '\n';
}

template <typename Actual, typename Expected>
void expect_equal(std::string_view test, const Actual& actual, const Expected& expected) {
    if (actual != expected) {
        fail(test, "values are not equal");
    }
}

template <typename Function>
void expect_protocol_error(std::string_view test, Function function) {
    try {
        function();
        fail(test, "expected ProtocolError");
    } catch (const minbot::protocol::ProtocolError&) {
    } catch (const std::exception& error) {
        fail(test, std::string("wrong exception: ") + error.what());
    }
}

void test_varint_examples() {
    using minbot::protocol::Bytes;
    using minbot::protocol::encode_varint;

    expect_equal("VarInt 0", encode_varint(0), Bytes{0x00});
    expect_equal("VarInt 1", encode_varint(1), Bytes{0x01});
    expect_equal("VarInt 127", encode_varint(127), Bytes{0x7F});
    expect_equal("VarInt 128", encode_varint(128), (Bytes{0x80, 0x01}));
    expect_equal("VarInt 255", encode_varint(255), (Bytes{0xFF, 0x01}));
    expect_equal(
        "VarInt max",
        encode_varint(std::numeric_limits<std::int32_t>::max()),
        (Bytes{0xFF, 0xFF, 0xFF, 0xFF, 0x07}));
    expect_equal("VarInt -1", encode_varint(-1), (Bytes{0xFF, 0xFF, 0xFF, 0xFF, 0x0F}));
    expect_equal(
        "VarInt min",
        encode_varint(std::numeric_limits<std::int32_t>::min()),
        (Bytes{0x80, 0x80, 0x80, 0x80, 0x08}));
}

void test_varint_round_trip() {
    constexpr std::array values{
        std::numeric_limits<std::int32_t>::min(),
        -1,
        0,
        1,
        127,
        128,
        255,
        2'097'151,
        std::numeric_limits<std::int32_t>::max(),
    };

    for (const auto value : values) {
        const auto bytes = minbot::protocol::encode_varint(value);
        std::size_t offset = 0;
        const auto decoded = minbot::protocol::read_varint(bytes, offset);
        expect_equal("VarInt round trip", decoded, value);
        expect_equal("VarInt consumed bytes", offset, bytes.size());
    }
}

void test_invalid_varints() {
    const minbot::protocol::Bytes truncated{0x80};
    expect_protocol_error("truncated VarInt", [&] {
        std::size_t offset = 0;
        static_cast<void>(minbot::protocol::read_varint(truncated, offset));
    });

    const minbot::protocol::Bytes overflow{0xFF, 0xFF, 0xFF, 0xFF, 0x10};
    expect_protocol_error("overflowing VarInt", [&] {
        std::size_t offset = 0;
        static_cast<void>(minbot::protocol::read_varint(overflow, offset));
    });
}

void test_strings() {
    minbot::protocol::Bytes bytes;
    const std::string expected = "hello, Minecraft";
    minbot::protocol::append_string(bytes, expected);

    std::size_t offset = 0;
    expect_equal("protocol string", minbot::protocol::read_string(bytes, offset), expected);
    expect_equal("protocol string consumed bytes", offset, bytes.size());

    const minbot::protocol::Bytes truncated{0x03, 'o', 'k'};
    expect_protocol_error("truncated string", [&] {
        std::size_t bad_offset = 0;
        static_cast<void>(minbot::protocol::read_string(truncated, bad_offset));
    });
}

void test_handshake() {
    const auto packet = minbot::protocol::make_handshake(767, "localhost", 25565);
    std::size_t offset = 0;
    const auto length = minbot::protocol::read_varint(packet, offset);
    expect_equal("handshake frame length", static_cast<std::size_t>(length), packet.size() - offset);
    expect_equal("handshake packet id", minbot::protocol::read_varint(packet, offset), 0);
    expect_equal("handshake protocol", minbot::protocol::read_varint(packet, offset), 767);
    expect_equal("handshake address", minbot::protocol::read_string(packet, offset), std::string("localhost"));

    if (offset + 2U > packet.size()) {
        fail("handshake port", "port bytes are missing");
        return;
    }
    const auto port = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(packet[offset]) << 8U) | packet[offset + 1U]);
    offset += 2U;
    expect_equal("handshake port", port, static_cast<std::uint16_t>(25565));
    expect_equal("handshake next state", minbot::protocol::read_varint(packet, offset), 1);
    expect_equal("handshake consumed bytes", offset, packet.size());

    const auto login_packet = minbot::protocol::make_handshake(767, "localhost", 25565, 2);
    offset = 0;
    static_cast<void>(minbot::protocol::read_varint(login_packet, offset));
    static_cast<void>(minbot::protocol::read_varint(login_packet, offset));
    static_cast<void>(minbot::protocol::read_varint(login_packet, offset));
    static_cast<void>(minbot::protocol::read_string(login_packet, offset));
    offset += 2U;
    expect_equal("login handshake next state", minbot::protocol::read_varint(login_packet, offset), 2);
}

void test_fixed_primitives() {
    minbot::protocol::Bytes bytes;
    minbot::protocol::append_i32_be(bytes, -123'456);
    minbot::protocol::append_bool(bytes, true);
    minbot::protocol::append_bool(bytes, false);

    std::size_t offset = 0;
    expect_equal("i32 round trip", minbot::protocol::read_i32_be(bytes, offset), -123'456);
    expect_equal("true boolean", minbot::protocol::read_bool(bytes, offset), true);
    expect_equal("false boolean", minbot::protocol::read_bool(bytes, offset), false);
    expect_equal("fixed primitives consumed bytes", offset, bytes.size());
}

void test_status_and_ping() {
    const std::string json = R"({"version":{"name":"1.21.1","protocol":767}})";
    minbot::protocol::Bytes status_body;
    minbot::protocol::append_varint(status_body, 0x00);
    minbot::protocol::append_string(status_body, json);
    expect_equal("status response", minbot::protocol::parse_status_response(status_body), json);

    constexpr std::int64_t payload = 1'234'567'890'123;
    minbot::protocol::Bytes pong_body;
    minbot::protocol::append_varint(pong_body, 0x01);
    minbot::protocol::append_i64_be(pong_body, payload);
    expect_equal("pong response", minbot::protocol::parse_pong_response(pong_body), payload);

    pong_body.push_back(0x00);
    expect_protocol_error("pong trailing byte", [&] {
        static_cast<void>(minbot::protocol::parse_pong_response(pong_body));
    });
}

}  // namespace

int main() {
    test_varint_examples();
    test_varint_round_trip();
    test_invalid_varints();
    test_strings();
    test_handshake();
    test_fixed_primitives();
    test_status_and_ping();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "All minbot " << minbot::version << " protocol tests passed\n";
    return 0;
}
