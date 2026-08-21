#include "minbot/control.hpp"
#include "minbot/version.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
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
void expect_invalid(std::string_view test, Function function) {
    try {
        function();
        fail(test, "expected invalid_argument");
    } catch (const std::invalid_argument&) {
    } catch (const std::exception& error) {
        fail(test, std::string("wrong exception: ") + error.what());
    }
}

void test_local_commands() {
    using Type = minbot::ControlCommand::Type;

    expect_equal("empty", minbot::parse_control_line("").type, Type::none);
    expect_equal("quit", minbot::parse_control_line("/quit").type, Type::quit);
    expect_equal("position", minbot::parse_control_line("/pos").type, Type::show_position);
    expect_equal("jump", minbot::parse_control_line("/jump").type, Type::jump);

    const auto move = minbot::parse_control_line("/move 12.5 -64 3e2");
    expect_equal("move type", move.type, Type::move);
    expect_equal("move x", move.x, 12.5);
    expect_equal("move y", move.y, -64.0);
    expect_equal("move z", move.z, 300.0);

    const auto look = minbot::parse_control_line("/look 180 -45.5");
    expect_equal("look type", look.type, Type::look);
    expect_equal("look yaw", look.yaw, 180.0F);
    expect_equal("look pitch", look.pitch, -45.5F);
}

void test_chat_fallback() {
    using Type = minbot::ControlCommand::Type;

    const auto chat = minbot::parse_control_line("hello world");
    expect_equal("chat type", chat.type, Type::chat);
    expect_equal("chat text", chat.text, std::string("hello world"));

    const auto server_command = minbot::parse_control_line("/home spawn");
    expect_equal("server command type", server_command.type, Type::chat);
    expect_equal("server command text", server_command.text, std::string("/home spawn"));
}

void test_invalid_commands() {
    expect_invalid("move arity", [] { minbot::parse_control_line("/move 1 2"); });
    expect_invalid("move text", [] { minbot::parse_control_line("/move one 2 3"); });
    expect_invalid("move infinity", [] { minbot::parse_control_line("/move inf 2 3"); });
    expect_invalid("look arity", [] { minbot::parse_control_line("/look 90"); });
    expect_invalid("quit arity", [] { minbot::parse_control_line("/quit now"); });
}

}  // namespace

int main() {
    test_local_commands();
    test_chat_fallback();
    test_invalid_commands();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "All minbot " << minbot::version << " control tests passed\n";
    return 0;
}
