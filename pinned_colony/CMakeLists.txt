# SPDX-FileCopyrightText: 2025-2026 Igal Alkon
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.28)
project(PinnedColonyECS CXX)

set(CMAKE_CXX_STANDARD 23)

add_executable(pinned_colony_ecs
        example_ecs.cpp
)
