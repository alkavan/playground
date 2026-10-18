// SPDX-FileCopyrightText: 2025-2026 Igal Alkon
// SPDX-License-Identifier: MIT

export module math;

export int add(const int a, const int b) {
    return a + b;
}

export int subtract(const int a, const int b) {
    return a - b;
}

export int multiply(const int a, const int b) {
    return a * b;
}

export double divide(const double a, const double b) {
    if (b == 0.0) {
        return 0.0; // Simple guard
    }
    return a / b;
}
