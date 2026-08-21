#include "minbot/control.hpp"

#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace minbot {
namespace {

std::vector<std::string_view> split_words(std::string_view line) {
    std::vector<std::string_view> words;
    std::size_t offset = 0;
    while (offset < line.size()) {
        while (offset < line.size() && (line[offset] == ' ' || line[offset] == '\t')) {
            ++offset;
        }
        if (offset == line.size()) {
            break;
        }
        const auto begin = offset;
        while (offset < line.size() && line[offset] != ' ' && line[offset] != '\t') {
            ++offset;
        }
        words.push_back(line.substr(begin, offset - begin));
    }
    return words;
}

template <typename Number>
Number parse_number(std::string_view value, std::string_view name) {
    Number result{};
    const auto* begin = value.data();
    const auto* end = begin + value.size();
    const auto parsed = std::from_chars(begin, end, result);
    if (parsed.ec != std::errc{} || parsed.ptr != end || !std::isfinite(result)) {
        throw std::invalid_argument(std::string(name) + " must be a finite number");
    }
    return result;
}

}  // namespace

ControlCommand parse_control_line(std::string_view line) {
    if (line.empty()) {
        return {};
    }

    const auto words = split_words(line);
    if (words.empty()) {
        return {};
    }
    if (words.front() == "/quit") {
        if (words.size() != 1U) {
            throw std::invalid_argument("usage: /quit");
        }
        return {ControlCommand::Type::quit};
    }
    if (words.front() == "/pos") {
        if (words.size() != 1U) {
            throw std::invalid_argument("usage: /pos");
        }
        return {ControlCommand::Type::show_position};
    }
    if (words.front() == "/jump") {
        if (words.size() != 1U) {
            throw std::invalid_argument("usage: /jump");
        }
        return {ControlCommand::Type::jump};
    }
    if (words.front() == "/move") {
        if (words.size() != 4U) {
            throw std::invalid_argument("usage: /move <x> <y> <z>");
        }
        ControlCommand command;
        command.type = ControlCommand::Type::move;
        command.x = parse_number<double>(words[1], "x");
        command.y = parse_number<double>(words[2], "y");
        command.z = parse_number<double>(words[3], "z");
        return command;
    }
    if (words.front() == "/look") {
        if (words.size() != 3U) {
            throw std::invalid_argument("usage: /look <yaw> <pitch>");
        }
        ControlCommand command;
        command.type = ControlCommand::Type::look;
        command.yaw = parse_number<float>(words[1], "yaw");
        command.pitch = parse_number<float>(words[2], "pitch");
        return command;
    }

    ControlCommand command;
    command.type = ControlCommand::Type::chat;
    command.text = line;
    return command;
}

}  // namespace minbot
