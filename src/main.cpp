#include "minbot/status_client.hpp"
#include "minbot/version.hpp"

#include <charconv>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void print_usage(std::ostream& output) {
    output
        << "minbot " << minbot::version << '\n'
        << "Minecraft Java status client built without bot libraries.\n\n"
        << "Usage:\n"
        << "  minbot --version\n"
        << "  minbot status <host> [port] [protocol-version]\n\n"
        << "Examples:\n"
        << "  minbot status localhost\n"
        << "  minbot status play.example.net 25565\n"
        << "  minbot status localhost 25565 767\n\n"
        << "The protocol version defaults to 767 for Minecraft Java 1.21.1.\n";
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

}  // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--version") {
            std::cout << "minbot " << minbot::version << '\n';
            return 0;
        }

        if (argc < 3 || argc > 5 || std::string_view(argv[1]) != "status") {
            print_usage(argc == 1 ? std::cout : std::cerr);
            return argc == 1 ? 0 : 2;
        }

        minbot::StatusOptions options;
        options.host = argv[2];

        if (argc >= 4) {
            const auto port = parse_integer<unsigned int>(argv[3], "port");
            if (port == 0U || port > std::numeric_limits<std::uint16_t>::max()) {
                throw std::invalid_argument("port must be between 1 and 65535");
            }
            options.port = static_cast<std::uint16_t>(port);
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
    } catch (const std::exception& error) {
        std::cerr << "minbot: " << error.what() << '\n';
        return 1;
    }
}
