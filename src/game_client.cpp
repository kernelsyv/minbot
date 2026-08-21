#include "minbot/game_client.hpp"

#include "minbot/protocol.hpp"

#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace minbot {
namespace {

using protocol::Byte;
using protocol::Bytes;

constexpr auto movement_interval = std::chrono::milliseconds(50);
constexpr double horizontal_position_limit = 30'000'000.0;
constexpr double vertical_position_limit = 2'048.0;
constexpr double jump_height = 0.42;

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

void validate_position(double x, double y, double z) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        throw std::invalid_argument("player coordinates must be finite");
    }
    if (std::abs(x) > horizontal_position_limit || std::abs(z) > horizontal_position_limit) {
        throw std::invalid_argument("x and z must stay within the 30,000,000 block limit");
    }
    if (std::abs(y) > vertical_position_limit) {
        throw std::invalid_argument("y must stay between -2048 and 2048");
    }
}

float normalize_yaw(float yaw) {
    if (!std::isfinite(yaw)) {
        throw std::invalid_argument("yaw must be finite");
    }
    return std::remainder(yaw, 360.0F);
}

void validate_pitch(float pitch) {
    if (!std::isfinite(pitch) || pitch < -90.0F || pitch > 90.0F) {
        throw std::invalid_argument("pitch must be between -90 and 90 degrees");
    }
}

Bytes make_position_packet(const PlayerState& state) {
    Bytes body;
    protocol::append_varint(body, 0x1A);
    protocol::append_f64_be(body, state.x);
    protocol::append_f64_be(body, state.y);
    protocol::append_f64_be(body, state.z);
    protocol::append_bool(body, state.on_ground);
    return body;
}

Bytes make_look_packet(const PlayerState& state) {
    Bytes body;
    protocol::append_varint(body, 0x1C);
    protocol::append_f32_be(body, state.yaw);
    protocol::append_f32_be(body, state.pitch);
    protocol::append_bool(body, state.on_ground);
    return body;
}

std::string describe_position(const PlayerState& state, std::string_view prefix) {
    std::ostringstream output;
    output << prefix << " x=" << std::fixed << std::setprecision(3) << state.x << " y=" << state.y
           << " z=" << state.z << " yaw=" << std::setprecision(1) << state.yaw
           << " pitch=" << state.pitch << " ground=" << (state.on_ground ? "yes" : "no");
    return output.str();
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

void GameClient::send_movement_body(Bytes body) {
    std::scoped_lock lock(movement_mutex_);
    const auto earliest = last_movement_sent_ + movement_interval;
    if (last_movement_sent_ != std::chrono::steady_clock::time_point{} &&
        std::chrono::steady_clock::now() < earliest) {
        std::this_thread::sleep_until(earliest);
    }
    send_body(std::move(body));
    last_movement_sent_ = std::chrono::steady_clock::now();
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

    while (state_.load() != State::play) {
        const auto packet = connection_->receive_packet();
        if (state_.load() == State::login) {
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
            "online-mode encryption is not supported; use a private offline-mode test server");
    case 0x02: {
        require_bytes(packet, offset, 16U);
        offset += 16U;
        const auto accepted_name = protocol::read_string(packet, offset, 16U);
        Bytes acknowledged;
        protocol::append_varint(acknowledged, 0x03);
        send_body(std::move(acknowledged));
        state_.store(State::configuration);
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
        state_.store(State::play);
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
        const auto x = protocol::read_f64_be(packet, offset);
        const auto y = protocol::read_f64_be(packet, offset);
        const auto z = protocol::read_f64_be(packet, offset);
        const auto yaw = protocol::read_f32_be(packet, offset);
        const auto pitch = protocol::read_f32_be(packet, offset);
        require_bytes(packet, offset, 1U);
        const auto flags = packet[offset++];
        if ((flags & 0xE0U) != 0U) {
            throw protocol::ProtocolError("position packet contains unknown relative flags");
        }
        const auto teleport_id = protocol::read_varint(packet, offset);

        PlayerState next;
        {
            std::scoped_lock lock(player_mutex_);
            next = player_state_;
            next.x = (flags & 0x01U) != 0U ? next.x + x : x;
            next.y = (flags & 0x02U) != 0U ? next.y + y : y;
            next.z = (flags & 0x04U) != 0U ? next.z + z : z;
            next.yaw = (flags & 0x08U) != 0U ? next.yaw + yaw : yaw;
            next.pitch = (flags & 0x10U) != 0U ? next.pitch + pitch : pitch;
            validate_position(next.x, next.y, next.z);
            next.yaw = normalize_yaw(next.yaw);
            validate_pitch(next.pitch);
            next.on_ground = false;
            next.initialized = true;
            player_state_ = next;
        }

        Bytes response;
        protocol::append_varint(response, 0x00);
        protocol::append_varint(response, teleport_id);
        send_body(std::move(response));
        return {GameEvent::Type::position, describe_position(
            next,
            "teleport " + std::to_string(teleport_id) + " ->")};
    }
    case 0x69: {
        Bytes acknowledged;
        protocol::append_varint(acknowledged, 0x0C);
        send_body(std::move(acknowledged));
        state_.store(State::configuration);
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
    if (state_.load() == State::configuration) {
        return handle_configuration_packet(packet);
    }
    if (state_.load() == State::play) {
        return handle_play_packet(packet);
    }
    throw std::logic_error("client is still in login state");
}

void GameClient::send_chat(std::string_view message) {
    if (state_.load() != State::play) {
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

PlayerState GameClient::move_to(double x, double y, double z) {
    if (state_.load() != State::play) {
        throw std::logic_error("movement is available only after joining the world");
    }
    validate_position(x, y, z);

    std::scoped_lock lock(player_mutex_);
    if (!player_state_.initialized) {
        throw std::logic_error("player position is not known yet; wait for the server teleport");
    }
    auto next = player_state_;
    next.x = x;
    next.y = y;
    next.z = z;
    next.on_ground = true;
    send_movement_body(make_position_packet(next));
    player_state_ = next;
    return next;
}

PlayerState GameClient::look(float yaw, float pitch) {
    if (state_.load() != State::play) {
        throw std::logic_error("look is available only after joining the world");
    }
    const auto normalized_yaw = normalize_yaw(yaw);
    validate_pitch(pitch);

    std::scoped_lock lock(player_mutex_);
    if (!player_state_.initialized) {
        throw std::logic_error("player position is not known yet; wait for the server teleport");
    }
    auto next = player_state_;
    next.yaw = normalized_yaw;
    next.pitch = pitch;
    send_movement_body(make_look_packet(next));
    player_state_ = next;
    return next;
}

PlayerState GameClient::jump() {
    if (state_.load() != State::play) {
        throw std::logic_error("jump is available only after joining the world");
    }

    std::scoped_lock lock(player_mutex_);
    if (!player_state_.initialized) {
        throw std::logic_error("player position is not known yet; wait for the server teleport");
    }

    const auto landing = player_state_;
    auto apex = landing;
    apex.y += jump_height;
    apex.on_ground = false;
    validate_position(apex.x, apex.y, apex.z);
    send_movement_body(make_position_packet(apex));
    player_state_ = apex;

    auto finished = landing;
    finished.on_ground = true;
    send_movement_body(make_position_packet(finished));
    player_state_ = finished;
    return finished;
}

void GameClient::close() noexcept {
    if (connection_) {
        connection_->close();
    }
}

bool GameClient::is_in_play() const noexcept {
    return state_.load() == State::play;
}

PlayerState GameClient::player_state() const {
    std::scoped_lock lock(player_mutex_);
    return player_state_;
}

std::size_t GameClient::loaded_chunk_count() const noexcept {
    return chunks_.size();
}

}  // namespace minbot
