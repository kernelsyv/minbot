#include "minbot/status_client.hpp"

#include "minbot/connection.hpp"
#include "minbot/protocol.hpp"

#include <chrono>
#include <stdexcept>

namespace minbot {
namespace {

std::int64_t unix_time_milliseconds() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

}  // namespace

StatusResult query_server_status(const StatusOptions& options) {
    if (options.host.empty()) {
        throw std::invalid_argument("host cannot be empty");
    }

    Connection connection(options.host, options.port, options.timeout_ms);
    connection.send_framed(protocol::make_handshake(
        options.protocol_version,
        options.host,
        options.port));
    connection.send_framed(protocol::make_status_request());

    StatusResult result;
    result.json = protocol::parse_status_response(connection.receive_packet());

    const auto ping_payload = unix_time_milliseconds();
    const auto started = std::chrono::steady_clock::now();
    connection.send_framed(protocol::make_ping_request(ping_payload));
    const auto echoed_payload = protocol::parse_pong_response(connection.receive_packet());
    const auto finished = std::chrono::steady_clock::now();
    if (echoed_payload != ping_payload) {
        throw protocol::ProtocolError("server returned a mismatched pong payload");
    }

    result.latency_ms = std::chrono::duration<double, std::milli>(finished - started).count();
    return result;
}

}  // namespace minbot
