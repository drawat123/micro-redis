// Unit tests for parse_port(), using GoogleTest.
//
// How GoogleTest works, in 30 seconds:
//   - TEST(SuiteName, CaseName) { ... } defines one test case.
//   - Inside, EXPECT_* / ASSERT_* macros check conditions. EXPECT keeps going
//     on failure (reports all failures); ASSERT stops the current test.
//   - You don't write a main() — GTest::gtest_main (linked in CMake) provides
//     one that discovers and runs every TEST in the binary.
//   - `ctest` (CMake's test runner) then executes the binary and reports pass/fail.

#include "config.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

TEST(ParsePort, AcceptsValidPorts) {
    EXPECT_EQ(parse_port("6380"), std::optional<std::uint16_t>(6380));
    EXPECT_EQ(parse_port("1"), std::optional<std::uint16_t>(1));
    EXPECT_EQ(parse_port("65535"), std::optional<std::uint16_t>(65535));
}

TEST(ParsePort, RejectsOutOfRange) {
    EXPECT_FALSE(parse_port("0").has_value());
    EXPECT_FALSE(parse_port("65536").has_value());
    EXPECT_FALSE(parse_port("-1").has_value());
    EXPECT_FALSE(parse_port("999999").has_value());
}

TEST(ParsePort, RejectsNonNumeric) {
    EXPECT_FALSE(parse_port("").has_value());
    EXPECT_FALSE(parse_port("abc").has_value());
    EXPECT_FALSE(parse_port("123abc").has_value());  // trailing junk
    EXPECT_FALSE(parse_port(" 80").has_value());     // leading space
}
