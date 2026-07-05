# Build description for micro-redis (rung 0, build-it-yourself edition).
#
# Configure once, then build (repeat as you edit):
#     cmake -S . -B build -G Ninja
#     cmake --build build
#     ./build/micro-redis
#
# This version compiles EVERY .cpp file in src/ into one program, so as you add
# files (say a server.cpp) they're picked up automatically — you won't need to
# edit this file. (Re-run the `cmake -S . -B build` step after adding a new file.)

cmake_minimum_required(VERSION 3.20)
project(micro_redis LANGUAGES CXX)

# Emit build/compile_commands.json — the "compilation database" that tells VS
# Code's IntelliSense the exact include paths and flags for every file. This is
# what makes autocomplete/go-to-definition accurate (no guessing).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to a Debug build (symbols for gdb, no optimizer).
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Warnings on, always — they catch a lot of C++ mistakes early.
add_compile_options(-Wall -Wextra -Wpedantic)

# Optional sanitizer build:  cmake -S . -B build -G Ninja -DMICRO_REDIS_SANITIZE=ON
# Then the program self-reports memory bugs (use-after-free, overflow, leaks) at runtime.
option(MICRO_REDIS_SANITIZE "Build with Address + UndefinedBehavior sanitizers" OFF)
if(MICRO_REDIS_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

file(GLOB SERVER_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_executable(micro-redis ${SERVER_SOURCES})

# Where the compiler looks for your headers. Because include/ is on this list,
# `#include "server.hpp"` finds include/server.hpp from any .cpp. (src/ is also
# listed so you may keep implementation-only "private" headers next to a .cpp.)
target_include_directories(micro-redis PRIVATE include src)

# (Unit tests come back at rung 1, when you have pure logic worth testing —
#  the RESP parser. The reference test setup is in reference/rung0/ for later.)
