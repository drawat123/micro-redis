# Test build: fetch GoogleTest, compile the test binary, register it with ctest.

include(FetchContent)

# FetchContent downloads GoogleTest at configure time and builds it as part of
# our project — so you don't have to install it system-wide, and the version is
# pinned and reproducible. (This needs network access the first time you run
# `cmake -S . -B build`; afterwards it's cached under ./build.)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
)
FetchContent_MakeAvailable(googletest)

# One test executable. Add more test_*.cpp files here as the project grows.
add_executable(unit_tests
    test_config.cpp
)

# Link our code under test + GoogleTest's ready-made main().
target_link_libraries(unit_tests PRIVATE
    microredis_core
    GTest::gtest_main
)

# gtest_discover_tests asks the binary for its list of TESTs and registers each
# with ctest, so `ctest` reports them individually.
include(GoogleTest)
gtest_discover_tests(unit_tests)
