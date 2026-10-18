/*
    Copyright (C) 2025 Igal Alkon and contributors

    Permission is hereby granted, free of charge, to any person obtaining a
    copy of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom the
    Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
*/

#ifndef OXIDE_HPP
#define OXIDE_HPP

#include <variant>
#include <string>
#include <utility>
#include <expected>
#include <optional>
#include <vector>

namespace oxide {
    template<typename T>
    using Option = std::optional<T>;

    // Optional type
    template<typename T>
    constexpr Option<std::decay_t<T>> Some(T&& value) {
        return std::make_optional(std::forward<T>(value));
    }

    constexpr std::nullopt_t None = std::nullopt;

    // Vector type
    template<typename T>
    using Vec = std::vector<T>;

    // Union type (like enum)
    template <typename... Variants>
    using Union = std::variant<Variants...>;

    // Pattern matching template
    template <class... Handlers>
    struct match : Handlers... {
        using Handlers::operator()...;
    };

    // Overload >> for visitation (as in your history)
    template <typename Variant, typename Matcher>
    void operator>>(Variant&& v, Matcher&& m) {
        std::visit(std::forward<Matcher>(m), std::forward<Variant>(v));
    }

    // Overloaded helper for macro-based matching (same as match but for separation)
    template <class... Ts>
    struct overloaded : Ts... {
        using Ts::operator()...;
    };

    // Use C++23 std::expected for Result<T, E> (Rust-like, with built-in methods)
    template <typename T, typename E = std::string>
    using Result = std::expected<T, E>;
}

// Macro-based match syntax for a more Rust-like feel (inspired by common emulations)
#define ox_match std::visit([](auto&&... args) { return oxide::overloaded{std::forward<decltype(args)>(args)...}; }
#define ox_match_case(param) [](param)
#define ox_match_value(value) , std::forward<decltype(value)>(value))

#endif // OXIDE_HPP
