# Build description for micro-redis.
#
# Configure once, then build (repeat as you edit):
#     cmake -S . -B build -G Ninja
#     cmake --build build
#     ./build/micro-redis
#     ctest --test-dir build --output-on-failure     # run the unit tests

cmake_minimum_required(VERSION 3.20)
project(micro_redis LANGUAGES CXX)

# Emit build/compile_commands.json for clangd (accurate code completion).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to a Debug build (symbols for gdb, no optimizer).
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

# Optional sanitizer build:  cmake -S . -B build -G Ninja -DMICRO_REDIS_SANITIZE=ON
option(MICRO_REDIS_SANITIZE "Build with Address + UndefinedBehavior sanitizers" OFF)
if(MICRO_REDIS_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

# --- Core library: ALL logic except main(). --------------------------------
# Both the server executable AND the unit tests link this, so your parser,
# store, and server code are all testable. Every src/*.cpp is picked up
# automatically (add a file, re-run the configure step); main.cpp is excluded
# because it holds the entry point, not reusable logic.
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(REMOVE_ITEM CORE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
add_library(microredis_core ${CORE_SOURCES})
target_include_directories(microredis_core PUBLIC include src)

# --- The server executable. ------------------------------------------------
add_executable(micro-redis src/main.cpp)
target_link_libraries(micro-redis PRIVATE microredis_core)

# --- Unit tests (GoogleTest). ----------------------------------------------
# Turn off with -DMICRO_REDIS_TESTS=OFF if you ever want a tests-free build.
option(MICRO_REDIS_TESTS "Build unit tests" ON)
if(MICRO_REDIS_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
