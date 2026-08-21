#include "minbot/control.hpp"
#include "minbot/game_client.hpp"
#include "minbot/status_client.hpp"
#include "minbot/version.hpp"

#include <atomic>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#ifdef _WIN32
#include <conio.h>
#include <io.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace {

void print_usage(std::ostream& output) {
    output
        << "minbot " << minbot::version << '\n'
        << "Minecraft Java client built without ready-made bot libraries.\n\n"
        << "Usage:\n"
        << "  minbot --version\n"
        << "  minbot status <host> [port] [protocol-version]\n"
        << "  minbot play <host> <username> [port] [auth-options]\n\n"
        << "Examples:\n"
        << "  minbot status localhost\n"
        << "  minbot play localhost MinBot 25565\n"
        << "  minbot play localhost MinBot --auth auto\n\n"
        << "Auth options:\n"
        << "  --auth <none|login|register|auto>\n"
        << "  --register-template <command>\n"
        << "  --login-template <command>\n"
        << "  --auth-delay-ms <0..10000>\n\n"
        << "The play command targets Minecraft Java 1.21.1 (protocol 767).\n"
        << "Authentication passwords are read without echo or from\n"
        << "MINBOT_AUTH_PASSWORD; they are never accepted as CLI arguments.\n";
}

template <typename Integer>
Integer parse_integer(std::string_view value, std::string_view name) {
    Integer result{};
    const auto* begin = value.data();
    const auto* end = begin + value.size();
    const auto parsed = std::from_chars(begin, end, result);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        throw std::invalid_argument(std::string(name) + " must be an integer");
    }
    return result;
}

std::uint16_t parse_port(std::string_view value) {
    const auto port = parse_integer<unsigned int>(value, "port");
    if (port == 0U || port > std::numeric_limits<std::uint16_t>::max()) {
        throw std::invalid_argument("port must be between 1 and 65535");
    }
    return static_cast<std::uint16_t>(port);
}

minbot::ServerAuthOptions::Mode parse_auth_mode(std::string_view value) {
    using Mode = minbot::ServerAuthOptions::Mode;
    if (value == "none") {
        return Mode::none;
    }
    if (value == "login") {
        return Mode::login;
    }
    if (value == "register") {
        return Mode::register_only;
    }
    if (value == "auto") {
        return Mode::register_then_login;
    }
    throw std::invalid_argument("auth mode must be none, login, register, or auto");
}

std::string read_secret() {
    std::cout << "Server auth password (hidden): " << std::flush;
#ifdef _WIN32
    if (_isatty(_fileno(stdin)) == 0) {
        throw std::runtime_error("set MINBOT_AUTH_PASSWORD when stdin is not a terminal");
    }
    std::string secret;
    for (;;) {
        const int character = _getch();
        if (character == '\r' || character == '\n') {
            std::cout << '\n';
            return secret;
        }
        if (character == 3) {
            std::cout << '\n';
            throw std::runtime_error("password input cancelled");
        }
        if (character == '\b') {
            if (!secret.empty()) {
                secret.pop_back();
            }
        } else if (character == 0 || character == 224) {
            static_cast<void>(_getch());
        } else if (character >= 32 && character <= 126) {
            secret.push_back(static_cast<char>(character));
        }
    }
#else
    if (isatty(STDIN_FILENO) == 0) {
        throw std::runtime_error("set MINBOT_AUTH_PASSWORD when stdin is not a terminal");
    }
    termios original{};
    if (tcgetattr(STDIN_FILENO, &original) != 0) {
        throw std::runtime_error("could not read terminal settings");
    }
    auto hidden = original;
    hidden.c_lflag &= static_cast<tcflag_t>(~ECHO);
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden) != 0) {
        throw std::runtime_error("could not hide password input");
    }
    std::string secret;
    const bool read = static_cast<bool>(std::getline(std::cin, secret));
    static_cast<void>(tcsetattr(STDIN_FILENO, TCSAFLUSH, &original));
    std::cout << '\n';
    if (!read) {
        throw std::runtime_error("password input cancelled");
    }
    return secret;
#endif
}

int run_status(int argc, char* argv[]) {
    if (argc < 3 || argc > 5) {
        print_usage(std::cerr);
        return 2;
    }

    minbot::StatusOptions options;
    options.host = argv[2];
    if (argc >= 4) {
        options.port = parse_port(argv[3]);
    }
    if (argc == 5) {
        options.protocol_version = parse_integer<std::int32_t>(argv[4], "protocol-version");
    }

    const auto result = minbot::query_server_status(options);
    std::cout
        << "Server: " << options.host << ':' << options.port << '\n'
        << "Ping: " << std::fixed << std::setprecision(2) << result.latency_ms << " ms\n"
        << "Status JSON:\n"
        << result.json << '\n';
    return 0;
}

void print_event(const minbot::GameEvent& event) {
    using Type = minbot::GameEvent::Type;
    switch (event.type) {
    case Type::joined:
        std::cout << "[world] joined as " << event.text << '\n';
        break;
    case Type::chunk_loaded:
        std::cout << "[chunk] loaded " << event.chunk_x << ", " << event.chunk_z << '\n';
        break;
    case Type::chunk_unloaded:
        std::cout << "[chunk] unloaded " << event.chunk_x << ", " << event.chunk_z << '\n';
        break;
    case Type::player_chat:
        std::cout << "[chat] " << event.text << '\n';
        break;
    case Type::system_chat:
        std::cout << "[system] " << event.text << '\n';
        break;
    case Type::position:
        std::cout << "[world] position " << event.text << '\n';
        break;
    case Type::disconnected:
        std::cout << "[world] " << event.text << '\n';
        break;
    case Type::none:
        break;
    }
}

void print_player_state(std::string_view label, const minbot::PlayerState& state) {
    if (!state.initialized) {
        std::cout << "[control] " << label << ": position not received from the server yet\n";
        return;
    }
    std::cout << "[control] " << label << ": x=" << std::fixed << std::setprecision(3) << state.x
              << " y=" << state.y << " z=" << state.z << " yaw=" << std::setprecision(1)
              << state.yaw << " pitch=" << state.pitch
              << " ground=" << (state.on_ground ? "yes" : "no") << '\n';
}

int run_play(int argc, char* argv[]) {
    if (argc < 4) {
        print_usage(std::cerr);
        return 2;
    }

    minbot::GameOptions options;
    options.host = argv[2];
    options.username = argv[3];
    int index = 4;
    if (index < argc && !std::string_view(argv[index]).starts_with("--")) {
        options.port = parse_port(argv[index++]);
    }
    while (index < argc) {
        const std::string_view flag = argv[index++];
        if (index >= argc) {
            throw std::invalid_argument(std::string(flag) + " requires a value");
        }
        const std::string_view value = argv[index++];
        if (flag == "--auth") {
            options.auth.mode = parse_auth_mode(value);
        } else if (flag == "--register-template") {
            options.auth.register_template = value;
        } else if (flag == "--login-template") {
            options.auth.login_template = value;
        } else if (flag == "--auth-delay-ms") {
            options.auth.delay_ms = parse_integer<int>(value, "auth-delay-ms");
        } else {
            throw std::invalid_argument("unknown play option: " + std::string(flag));
        }
    }

    if (options.auth.mode != minbot::ServerAuthOptions::Mode::none) {
        if (const auto* password = std::getenv("MINBOT_AUTH_PASSWORD"); password != nullptr) {
            options.auth.password = password;
        } else {
            options.auth.password = read_secret();
        }
    }

    minbot::GameClient client(options);
    std::cout << "Connecting to " << options.host << ':' << options.port << "...\n";
    client.connect();
    if (options.auth.mode != minbot::ServerAuthOptions::Mode::none) {
        client.authenticate();
        std::cout << "Server register/login command sequence sent.\n";
    }
    std::cout
        << "Joined the play state. Local controls: /pos, /move <x> <y> <z>,\n"
        << "/look <yaw> <pitch>, /jump, and /quit. Other lines are sent to chat.\n";

    std::atomic_bool stop = false;
    std::exception_ptr receiver_error;
    std::mutex output_mutex;
    std::thread receiver([&] {
        try {
            while (!stop.load()) {
                const auto event = client.process_next();
                if (event.type != minbot::GameEvent::Type::none) {
                    std::scoped_lock lock(output_mutex);
                    print_event(event);
                }
                if (event.type == minbot::GameEvent::Type::disconnected) {
                    stop.store(true);
                }
            }
        } catch (...) {
            if (!stop.load()) {
                receiver_error = std::current_exception();
                stop.store(true);
            }
        }
    });

    std::string line;
    while (!stop.load() && std::getline(std::cin, line)) {
        try {
            const auto command = minbot::parse_control_line(line);
            using CommandType = minbot::ControlCommand::Type;
            switch (command.type) {
            case CommandType::quit:
                stop.store(true);
                break;
            case CommandType::show_position: {
                const auto state = client.player_state();
                std::scoped_lock lock(output_mutex);
                print_player_state("position", state);
                break;
            }
            case CommandType::move: {
                const auto state = client.move_to(command.x, command.y, command.z);
                std::scoped_lock lock(output_mutex);
                print_player_state("moved", state);
                break;
            }
            case CommandType::look: {
                const auto state = client.look(command.yaw, command.pitch);
                std::scoped_lock lock(output_mutex);
                print_player_state("look", state);
                break;
            }
            case CommandType::jump: {
                const auto state = client.jump();
                std::scoped_lock lock(output_mutex);
                print_player_state("jumped", state);
                break;
            }
            case CommandType::chat:
                client.send_chat(command.text);
                break;
            case CommandType::none:
                break;
            }
            if (command.type == CommandType::quit) {
                break;
            }
        } catch (const std::exception& error) {
            std::scoped_lock lock(output_mutex);
            std::cerr << "[control] " << error.what() << '\n';
        }
    }

    stop.store(true);
    client.close();
    receiver.join();
    if (receiver_error) {
        std::rethrow_exception(receiver_error);
    }
    std::cout << "Stopped. Tracked chunks: " << client.loaded_chunk_count() << '\n';
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--version") {
            std::cout << "minbot " << minbot::version << '\n';
            return 0;
        }
        if (argc == 1) {
            print_usage(std::cout);
            return 0;
        }
        if (std::string_view(argv[1]) == "status") {
            return run_status(argc, argv);
        }
        if (std::string_view(argv[1]) == "play") {
            return run_play(argc, argv);
        }
        print_usage(std::cerr);
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "minbot: " << error.what() << '\n';
        return 1;
    }
}
