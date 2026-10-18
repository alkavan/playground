// SPDX-FileCopyrightText: 2025-2026 Igal Alkon
// SPDX-License-Identifier: MIT

#include <iostream>

#include "oxide.hpp"

// Define shape types (no inheritance)
namespace {
    struct Circle {
        double radius = 1.0;

        [[nodiscard]] double area() const {
            return 3.14159 * radius * radius;
        }
    };


    struct Rectangle {
        double width = 7.0, height = 14.0;

        [[nodiscard]] double perimeter() const {
            return 2 * (width + height);
        }
    };
}

// Create a discriminated union for shapes
using ShapeVariant = oxide::Union<Circle, Rectangle>;

// Clone function using pattern matching on the Union
static ShapeVariant clone(const ShapeVariant& shape) {
    return std::visit(oxide::overloaded{
        [](const Circle& c) -> ShapeVariant { return Circle{c.radius}; },
        [](const Rectangle& r) -> ShapeVariant { return Rectangle{.width = r.width, .height = r.height}; }
    }, shape);
}

int main() {
    constexpr ShapeVariant circle = Circle{};
    auto cloned_circle = clone(circle);

    constexpr ShapeVariant rectangle = Rectangle{};
    auto cloned_rectangle = clone(rectangle);

    // Use oxide's match and operator>> to polymorphically compute and print properties (akin to covariant dispatch)
    cloned_circle >> oxide::match{
        [](const Circle& c) { std::cout << "Cloned Circle area: " << c.area() << std::endl; },
        [](const Rectangle& r) { std::cout << "Cloned Rectangle perimeter: " << r.perimeter() << std::endl; }
    };

    cloned_rectangle >> oxide::match{
        [](const Circle& c) { std::cout << "Cloned Circle area: " << c.area() << std::endl; },
        [](const Rectangle& r) { std::cout << "Cloned Rectangle perimeter: " << r.perimeter() << std::endl; }
    };

    return 0;
}
