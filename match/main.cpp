// SPDX-FileCopyrightText: 2025-2026 Igal Alkon
// SPDX-License-Identifier: MIT

#include "oxide.hpp"

#include <functional>
#include <array>
#include <iostream>

namespace {
    struct Quit {}; // Unit variant
    struct Move { int x, y; };  // Struct variant
    struct Write { std::string text; };  // Tuple-like (but using struct for named field)
    struct Read { std::function<void()> callback; };  // Variant holding a lambda or function
}

using Message = oxide::Union<Quit, Move, Write, Read>;

// Non-template factory functions
static constexpr Message quit() { return Message{Quit{}}; }
static constexpr Message move_to(const int x, const int y) { return Message{Move{.x = x, .y = y}}; }
static Message write(std::string text) { return Message{Write{std::move(text)}}; }
static Message read(std::function<void()> callback) { return Message{Read{std::move(callback)}}; }

// Function that might find a message by type (returns Option with copy/move)
static oxide::Option<Message> find_move_message(const std::vector<Message>& messages) {
    for (const auto& msg : messages) {
        if (std::holds_alternative<Move>(msg)) {
            return oxide::Some(msg);  // This copies the message
        }
    }
    return oxide::None;
}

// Alternative: return index to avoid copying
static oxide::Option<size_t> find_move_message_index(const std::vector<Message>& messages) {
    for (size_t i = 0; i < messages.size(); ++i) {
        if (std::holds_alternative<Move>(messages[i])) {
            return oxide::Some(i);
        }
    }
    return oxide::None;
}

// Function that extracts coordinates if message is Move - takes by value for and_then compatibility
static oxide::Option<std::pair<int, int>> get_coordinates(const Message &msg) {
    if (const auto* move_msg = std::get_if<Move>(&msg)) {
        return oxide::Some(std::make_pair(move_msg->x, move_msg->y));
    }
    return oxide::None;
}

// Alternative version that takes by reference for direct use
static oxide::Option<std::pair<int, int>> get_coordinates_ref(const Message& msg) {
    if (const auto* move_msg = std::get_if<Move>(&msg)) {
        return oxide::Some(std::make_pair(move_msg->x, move_msg->y));
    }
    return oxide::None;
}

// Usage example
int main() {

    namespace ox = oxide;
    using ox::Result;
    using ox::Vec;

    std::array msgs = {
        quit(),           // No template parameter needed
        move_to(1, 2),
        write("hello"),
        read([](){std::cout << "Reading...\n";})
    };

    for (auto& msg : msgs) {
        // Existing match syntax
        msg >> ox::match {
            [](const Quit&) { std::cout << "Quit\n"; },
            [](const Move& m) { std::cout << "Move: (" << m.x << ", " << m.y << ")\n"; },
            [](const Write& w) { std::cout << "Write: " << w.text << "\n"; },
            [](const Read& r) { r.callback(); }
        };

        // Macro-based match with fixed syntax (use parentheses for the cases)
        ox_match (
            ox_match_case(const Quit&) { std::cout << "Quit (macro)\n"; },
            ox_match_case(const Move& m) { std::cout << "Move (macro): (" << m.x << ", " << m.y << ")\n"; },
            ox_match_case(const Write& w) { std::cout << "Write (macro): " << w.text << "\n"; },
            ox_match_case(const Read& r) { r.callback(); },
            // Optional: Catch-all for unhandled types (runtime fallback; defeats compile-time exhaustiveness)
            ox_match_case(auto&&) { std::cout << "Unhandled variant\n"; }
        ) ox_match_value(msg);
    }

    // Optional configuration
    ox::Option<std::string> user_name = ox::Some(std::string("Player1"));
    ox::Option<int> max_moves = ox::None;  // Not configured

    std::cout << "User: " << user_name.value_or("Anonymous") << "\n";
    std::cout << "Max moves: " << max_moves.value_or(100) << "\n";

    // Convert to vector for the find function
    Vec<Message> msg_vec(msgs.begin(), msgs.end());

    // Using Option to find a specific message type
    if (auto found_move = find_move_message(msg_vec); found_move.has_value()) {
        std::cout << "Found a Move message!\n";

        // Chain operations with Option - now works because get_coordinates takes by value
        if (auto coords = found_move.and_then(get_coordinates)) {
            std::cout << "Move coordinates: (" << coords->first << ", " << coords->second << ")\n";
        }

        // Alternative: use the reference version directly
        if (auto coords = get_coordinates_ref(*found_move)) {
            std::cout << "Move coordinates (direct): (" << coords->first << ", " << coords->second << ")\n";
        }
    } else {
        std::cout << "No Move message found\n";
    }

    // Example with optional settings affecting processing
    auto process_with_context = [&](const Message& msg) {
        msg >> ox::match {
            [&user_name](const Quit&) {
                std::cout << user_name.value_or("Someone") << " wants to quit\n";
            },
            [&max_moves](const Move& m) {
                std::cout << "Processing move: (" << m.x << ", " << m.y << ")";
                if (max_moves) {
                    std::cout << " (limit: " << *max_moves << ")";
                }
                std::cout << "\n";
            },
            [](const Write& w) { std::cout << "Processing write: " << w.text << "\n"; },
            [](const Read& r) { std::cout << "Processing read operation\n"; }
        };
    };

    std::cout << "\nProcessing with context:\n";
    for (const auto& msg : msg_vec) {
        process_with_context(msg);
    }

    // Rust-like example with std::expected (built-in monadic ops: and_then, transform, etc.)
    auto divide = [](const int a, const int b) -> Result<int> {
        if (b == 0) return std::unexpected("Division by zero");
        return a / b;
    };

    Result<int> ok_res = divide(84, 2);
    Result<int> err_res = divide(84, 0);

    // Built-in methods: has_value() like is_ok, value() like unwrap (throws if error), error(), value_or(default)
    if (ok_res.has_value()) {
        std::cout << "Ok: " << ok_res.value() << "\n";
    } else {
        std::cout << "Err: " << ok_res.error() << "\n";
    }

    // Monadic chaining (Rust-like map/and_then) - capture divide by reference
    const auto chained = ok_res.and_then([&divide](const int val) -> Result<int> {
        return divide(val, 3);  // Now divide is accessible via capture
    }).transform([](const int val) { return val * 2; });  // Maps success value

    if (chained.has_value()) {
        std::cout << "Chained Ok: " << chained.value() << "\n";
    }

    // For err_res, similar handling or or_else for recovery
    const auto recovered = err_res.or_else([](const std::string&) -> Result<int> {
        return 0;  // Recover from error
    });
    std::cout << "Recovered: " << recovered.value_or(-1) << "\n";

    return 0;
}