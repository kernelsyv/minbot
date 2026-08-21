#pragma once

#include <string>
#include <string_view>

namespace minbot {

struct ControlCommand {
    enum class Type { none, chat, quit, show_position, move, look, jump };

    Type type = Type::none;
    std::string text;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    float yaw = 0.0F;
    float pitch = 0.0F;
};

ControlCommand parse_control_line(std::string_view line);

}  // namespace minbot
