# Test build: fetch GoogleTest, compile every tests/*.cpp into one binary,
# register each TEST with ctest.

include(FetchContent)

# Downloads GoogleTest at configure time and builds it as part of the project —
# no system install needed, version pinned & reproducible. (Needs network the
# first time you configure; afterwards it's cached under build/_deps.)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
)
FetchContent_MakeAvailable(googletest)

# Every tests/*.cpp is picked up automatically. Add test_resp.cpp here just by
# creating the file (then re-run the cmake configure step).
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS *.cpp)

add_executable(unit_tests ${TEST_SOURCES})

# Link the code under test (microredis_core) + GoogleTest's ready-made main().
# Because microredis_core exposes include/ and src/ as PUBLIC include dirs,
# tests can do #include "resp.hpp" etc. with no extra config.
target_link_libraries(unit_tests PRIVATE
    microredis_core
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(unit_tests)
