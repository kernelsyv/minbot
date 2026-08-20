#pragma once

#include <cstdint>
#include <string>

namespace minbot {

struct StatusOptions {
    std::string host;
    std::uint16_t port = 25565;
    std::int32_t protocol_version = 767;
    int timeout_ms = 5000;
};

struct StatusResult {
    std::string json;
    double latency_ms = 0.0;
};

StatusResult query_server_status(const StatusOptions& options);

}  // namespace minbot
