#pragma once

#include "minbot/connection.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace minbot {

struct ServerAuthOptions {
    enum class Mode { none, login, register_only, register_then_login };

    Mode mode = Mode::none;
    std::string password;
    std::string register_template = "/register {password} {password}";
    std::string login_template = "/login {password}";
    int delay_ms = 750;
};

struct GameOptions {
    std::string host;
    std::string username;
    std::uint16_t port = 25565;
    std::int32_t protocol_version = 767;
    int timeout_ms = 30'000;
    ServerAuthOptions auth;
};

struct ChunkSummary {
    std::int32_t x = 0;
    std::int32_t z = 0;
    std::size_t packet_bytes = 0;
};

struct GameEvent {
    enum class Type {
        none,
        joined,
        chunk_loaded,
        chunk_unloaded,
        player_chat,
        system_chat,
        position,
        disconnected,
    };

    Type type = Type::none;
    std::string text;
    std::int32_t chunk_x = 0;
    std::int32_t chunk_z = 0;
};

class GameClient {
public:
    explicit GameClient(GameOptions options);
    ~GameClient();

    GameClient(const GameClient&) = delete;
    GameClient& operator=(const GameClient&) = delete;

    void connect();
    GameEvent process_next();
    void send_chat(std::string_view message);
    void authenticate();
    void close() noexcept;

    [[nodiscard]] bool is_in_play() const noexcept;
    [[nodiscard]] std::size_t loaded_chunk_count() const noexcept;

private:
    enum class State { login, configuration, play };

    void send_body(protocol::Bytes body);
    void send_client_settings(std::int32_t packet_id);
    void handle_login_packet(std::span<const protocol::Byte> packet);
    GameEvent handle_configuration_packet(std::span<const protocol::Byte> packet);
    GameEvent handle_play_packet(std::span<const protocol::Byte> packet);

    GameOptions options_;
    std::unique_ptr<Connection> connection_;
    State state_ = State::login;
    std::unordered_map<std::uint64_t, ChunkSummary> chunks_;
};

}  // namespace minbot
