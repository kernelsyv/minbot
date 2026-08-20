#include "minbot/game_client.hpp"

#include "minbot/protocol.hpp"

#include <array>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace minbot {
namespace {

using protocol::Byte;
using protocol::Bytes;

std::uint64_t chunk_key(std::int32_t x, std::int32_t z) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32U) |
        static_cast<std::uint32_t>(z);
}

void require_bytes(std::span<const Byte> input, std::size_t offset, std::size_t count) {
    if (offset > input.size() || count > input.size() - offset) {
        throw protocol::ProtocolError("truncated game packet");
    }
}

std::array<Byte, 16> offline_uuid(std::string_view username) {
    constexpr std::uint64_t offset_basis = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t first = offset_basis;
    std::uint64_t second = offset_basis ^ 0x9E3779B97F4A7C15ULL;
    const std::string source = "OfflinePlayer:" + std::string(username);
    for (const auto character : source) {
        first = (first ^ static_cast<unsigned char>(character)) * prime;
        second = (second ^ static_cast<unsigned char>(character)) * (prime + 2U);
    }

    std::array<Byte, 16> uuid{};
    for (std::size_t index = 0; index < 8U; ++index) {
        uuid[index] = static_cast<Byte>(first >> ((7U - index) * 8U));
        uuid[index + 8U] = static_cast<Byte>(second >> ((7U - index) * 8U));
    }
    uuid[6] = static_cast<Byte>((uuid[6] & 0x0FU) | 0x40U);
    uuid[8] = static_cast<Byte>((uuid[8] & 0x3FU) | 0x80U);
    return uuid;
}

void validate_username(std::string_view username) {
    if (username.size() < 3U || username.size() > 16U) {
        throw std::invalid_argument("username must contain between 3 and 16 characters");
    }
    for (const auto character : username) {
        if (std::isalnum(static_cast<unsigned char>(character)) == 0 && character != '_') {
            throw std::invalid_argument("username may contain only letters, digits, and underscore");
        }
    }
}

std::int64_t unix_time_milliseconds() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

void replace_all(std::string& value, std::string_view placeholder, std::string_view replacement) {
    std::size_t offset = 0;
    while ((offset = value.find(placeholder, offset)) != std::string::npos) {
        value.replace(offset, placeholder.size(), replacement);
        offset += replacement.size();
    }
}

std::string expand_auth_command(
    std::string_view command_template,
    std::string_view username,
    std::string_view password) {
    std::string command(command_template);
    replace_all(command, "{username}", username);
    replace_all(command, "{password}", password);
    if (command.find('{') != std::string::npos || command.find('}') != std::string::npos) {
        throw std::invalid_argument("auth command contains an unknown placeholder");
    }
    if (command.empty() || command.front() != '/') {
        throw std::invalid_argument("auth command template must begin with /");
    }
    if (command.size() > 256U) {
        throw std::invalid_argument("expanded auth command exceeds 256 bytes");
    }
    return command;
}

}  // namespace

GameClient::GameClient(GameOptions options) : options_(std::move(options)) {
    if (options_.host.empty()) {
        throw std::invalid_argument("host cannot be empty");
    }
    validate_username(options_.username);
}

GameClient::~GameClient() = default;

void GameClient::send_body(Bytes body) {
    connection_->send_packet(body);
}

void GameClient::send_client_settings(std::int32_t packet_id) {
    Bytes body;
    protocol::append_varint(body, packet_id);
    protocol::append_string(body, "en_us");
    body.push_back(8U);
    protocol::append_varint(body, 0);
    protocol::append_bool(body, true);
    body.push_back(0x7FU);
    protocol::append_varint(body, 1);
    protocol::append_bool(body, false);
    protocol::append_bool(body, true);
    send_body(std::move(body));
}

void GameClient::connect() {
    connection_ = std::make_unique<Connection>(options_.host, options_.port, options_.timeout_ms);
    connection_->send_framed(protocol::make_handshake(
        options_.protocol_version,
        options_.host,
        options_.port,
        2));

    Bytes login_start;
    protocol::append_varint(login_start, 0x00);
    protocol::append_string(login_start, options_.username);
    const auto uuid = offline_uuid(options_.username);
    login_start.insert(login_start.end(), uuid.begin(), uuid.end());
    send_body(std::move(login_start));

    while (state_ != State::play) {
        const auto packet = connection_->receive_packet();
        if (state_ == State::login) {
            handle_login_packet(packet);
        } else {
            static_cast<void>(handle_configuration_packet(packet));
        }
    }
}

void GameClient::handle_login_packet(std::span<const Byte> packet) {
    std::size_t offset = 0;
    const auto packet_id = protocol::read_varint(packet, offset);
    switch (packet_id) {
    case 0x00:
        throw std::runtime_error("server rejected login: " + protocol::read_string(packet, offset));
    case 0x01:
        throw std::runtime_error(
            "online-mode encryption is not supported in 0.1.1; use a private offline-mode test server");
    case 0x02: {
        require_bytes(packet, offset, 16U);
        offset += 16U;
        const auto accepted_name = protocol::read_string(packet, offset, 16U);
        Bytes acknowledged;
        protocol::append_varint(acknowledged, 0x03);
        send_body(std::move(acknowledged));
        state_ = State::configuration;
        send_client_settings(0x00);
        options_.username = accepted_name;
        return;
    }
    case 0x03: {
        const auto threshold = protocol::read_varint(packet, offset);
        connection_->enable_compression(threshold);
        return;
    }
    case 0x04: {
        const auto message_id = protocol::read_varint(packet, offset);
        Bytes response;
        protocol::append_varint(response, 0x02);
        protocol::append_varint(response, message_id);
        protocol::append_bool(response, false);
        send_body(std::move(response));
        return;
    }
    case 0x05: {
        const auto key = protocol::read_string(packet, offset);
        Bytes response;
        protocol::append_varint(response, 0x04);
        protocol::append_string(response, key);
        protocol::append_bool(response, false);
        send_body(std::move(response));
        return;
    }
    default:
        return;
    }
}

GameEvent GameClient::handle_configuration_packet(std::span<const Byte> packet) {
    std::size_t offset = 0;
    const auto packet_id = protocol::read_varint(packet, offset);
    switch (packet_id) {
    case 0x00: {
        const auto key = protocol::read_string(packet, offset);
        Bytes response;
        protocol::append_varint(response, 0x01);
        protocol::append_string(response, key);
        protocol::append_bool(response, false);
        send_body(std::move(response));
        break;
    }
    case 0x02:
        throw std::runtime_error("server disconnected during configuration");
    case 0x03: {
        Bytes finished;
        protocol::append_varint(finished, 0x03);
        send_body(std::move(finished));
        state_ = State::play;
        return {GameEvent::Type::joined, options_.username};
    }
    case 0x04: {
        const auto keep_alive = protocol::read_i64_be(packet, offset);
        Bytes response;
        protocol::append_varint(response, 0x04);
        protocol::append_i64_be(response, keep_alive);
        send_body(std::move(response));
        break;
    }
    case 0x05: {
        const auto ping = protocol::read_i32_be(packet, offset);
        Bytes response;
        protocol::append_varint(response, 0x05);
        protocol::append_i32_be(response, ping);
        send_body(std::move(response));
        break;
    }
    case 0x0E: {
        Bytes response;
        protocol::append_varint(response, 0x07);
        protocol::append_varint(response, 0);
        send_body(std::move(response));
        break;
    }
    default:
        break;
    }
    return {};
}

GameEvent GameClient::handle_play_packet(std::span<const Byte> packet) {
    std::size_t offset = 0;
    const auto packet_id = protocol::read_varint(packet, offset);
    switch (packet_id) {
    case 0x0C: {
        const auto batch_size = protocol::read_varint(packet, offset);
        Bytes response;
        protocol::append_varint(response, 0x08);
        protocol::append_f32_be(response, batch_size > 0 ? 20.0F : 1.0F);
        send_body(std::move(response));
        break;
    }
    case 0x1D:
        return {GameEvent::Type::disconnected, "server disconnected the bot"};
    case 0x1E:
        return {GameEvent::Type::system_chat, "[decorated server chat received]"};
    case 0x21: {
        const auto z = protocol::read_i32_be(packet, offset);
        const auto x = protocol::read_i32_be(packet, offset);
        chunks_.erase(chunk_key(x, z));
        return {GameEvent::Type::chunk_unloaded, {}, x, z};
    }
    case 0x26: {
        const auto keep_alive = protocol::read_i64_be(packet, offset);
        Bytes response;
        protocol::append_varint(response, 0x18);
        protocol::append_i64_be(response, keep_alive);
        send_body(std::move(response));
        break;
    }
    case 0x27: {
        const auto x = protocol::read_i32_be(packet, offset);
        const auto z = protocol::read_i32_be(packet, offset);
        chunks_.insert_or_assign(chunk_key(x, z), ChunkSummary{x, z, packet.size()});
        return {GameEvent::Type::chunk_loaded, {}, x, z};
    }
    case 0x2B: {
        const auto entity_id = protocol::read_i32_be(packet, offset);
        return {GameEvent::Type::joined, "entity " + std::to_string(entity_id)};
    }
    case 0x39: {
        require_bytes(packet, offset, 16U);
        offset += 16U;
        static_cast<void>(protocol::read_varint(packet, offset));
        if (protocol::read_bool(packet, offset)) {
            require_bytes(packet, offset, 256U);
            offset += 256U;
        }
        return {GameEvent::Type::player_chat, protocol::read_string(packet, offset, 256U)};
    }
    case 0x40: {
        require_bytes(packet, offset, 33U);
        offset += 33U;
        const auto teleport_id = protocol::read_varint(packet, offset);
        Bytes response;
        protocol::append_varint(response, 0x00);
        protocol::append_varint(response, teleport_id);
        send_body(std::move(response));
        return {GameEvent::Type::position, "teleport " + std::to_string(teleport_id)};
    }
    case 0x69: {
        Bytes acknowledged;
        protocol::append_varint(acknowledged, 0x0C);
        send_body(std::move(acknowledged));
        state_ = State::configuration;
        break;
    }
    case 0x6C:
        return {GameEvent::Type::system_chat, "[system chat received]"};
    default:
        break;
    }
    return {};
}

GameEvent GameClient::process_next() {
    if (!connection_) {
        throw std::logic_error("connect must be called before processing packets");
    }
    const auto packet = connection_->receive_packet();
    if (state_ == State::configuration) {
        return handle_configuration_packet(packet);
    }
    if (state_ == State::play) {
        return handle_play_packet(packet);
    }
    throw std::logic_error("client is still in login state");
}

void GameClient::send_chat(std::string_view message) {
    if (state_ != State::play) {
        throw std::logic_error("chat is available only after joining the world");
    }
    if (message.empty() || message.size() > 256U) {
        throw std::invalid_argument("chat message must contain between 1 and 256 bytes");
    }

    Bytes body;
    if (message.front() == '/' && message.size() > 1U) {
        protocol::append_varint(body, 0x04);
        protocol::append_string(body, message.substr(1U));
    } else {
        protocol::append_varint(body, 0x06);
        protocol::append_string(body, message);
        protocol::append_i64_be(body, unix_time_milliseconds());
        protocol::append_i64_be(body, 0);
        protocol::append_bool(body, false);
        protocol::append_varint(body, 0);
        body.insert(body.end(), 3U, 0U);
    }
    send_body(std::move(body));
}

void GameClient::authenticate() {
    const auto& auth = options_.auth;
    if (auth.mode == ServerAuthOptions::Mode::none) {
        return;
    }
    if (auth.password.empty()) {
        throw std::invalid_argument("server authentication password cannot be empty");
    }
    if (auth.delay_ms < 0 || auth.delay_ms > 10'000) {
        throw std::invalid_argument("auth delay must be between 0 and 10000 milliseconds");
    }

    if (auth.mode == ServerAuthOptions::Mode::register_only ||
        auth.mode == ServerAuthOptions::Mode::register_then_login) {
        send_chat(expand_auth_command(
            auth.register_template,
            options_.username,
            auth.password));
    }

    if (auth.mode == ServerAuthOptions::Mode::register_then_login) {
        std::this_thread::sleep_for(std::chrono::milliseconds(auth.delay_ms));
    }
    if (auth.mode == ServerAuthOptions::Mode::login ||
        auth.mode == ServerAuthOptions::Mode::register_then_login) {
        send_chat(expand_auth_command(
            auth.login_template,
            options_.username,
            auth.password));
    }
}

void GameClient::close() noexcept {
    if (connection_) {
        connection_->close();
    }
}

bool GameClient::is_in_play() const noexcept {
    return state_ == State::play;
}

std::size_t GameClient::loaded_chunk_count() const noexcept {
    return chunks_.size();
}

}  // namespace minbot
