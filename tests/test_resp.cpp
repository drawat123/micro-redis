// Unit tests for parse_command() — the RESP request parser.
//
// GoogleTest patterns you'll reuse:
//   TEST(SuiteName, CaseName) { ... }   defines one test case.
//   EXPECT_EQ(a, b)  checks a == b but KEEPS GOING on failure (reports all).
//   ASSERT_EQ(a, b)  checks a == b and STOPS this test on failure — use it as a
//                    guard before code that would crash if the check failed
//                    (e.g. don't index args[0] unless args has the right size).
//
// Lifetime note: parse_command returns string_views INTO its input. So we keep
// each input in a named `std::string` that outlives the ParseResult — never pass
// a temporary, or the views would dangle.

#include "resp.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

using SV = std::string_view;
using Args = std::vector<std::string_view>;

// ---- Ok: complete commands ----------------------------------------------

TEST(ParseCommand, ParsesSetFooBar) {
    const std::string input = "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n";
    auto r = parse_command(input);

    ASSERT_EQ(r.status, ParseStatus::Ok);
    EXPECT_EQ(r.args, (Args{"SET", "foo", "bar"}));
    EXPECT_EQ(r.consumed, input.size());  // consumed the whole buffer
}

TEST(ParseCommand, ParsesPing) {
    const std::string input = "*1\r\n$4\r\nPING\r\n";
    auto r = parse_command(input);

    ASSERT_EQ(r.status, ParseStatus::Ok);
    EXPECT_EQ(r.args, (Args{"PING"}));
    EXPECT_EQ(r.consumed, input.size());
}

// ---- Incomplete: need more bytes (consume nothing) ----------------------

TEST(ParseCommand, EmptyInputIsIncomplete) {
    const std::string input = "";
    auto r = parse_command(input);

    EXPECT_EQ(r.status, ParseStatus::Incomplete);
    EXPECT_EQ(r.consumed, 0u);
}

TEST(ParseCommand, TruncatedMidBulkDataIsIncomplete) {
    // "$3" promises 3 bytes but only "SE" is present.
    const std::string input = "*3\r\n$3\r\nSE";
    auto r = parse_command(input);

    EXPECT_EQ(r.status, ParseStatus::Incomplete);
    EXPECT_EQ(r.consumed, 0u);
}

TEST(ParseCommand, TruncatedArrayHeaderIsIncomplete) {
    const std::string input = "*3\r\n";  // count parsed, but no elements yet
    auto r = parse_command(input);

    EXPECT_EQ(r.status, ParseStatus::Incomplete);
}

TEST(ParseCommand, UnterminatedCountLineIsIncomplete) {
    const std::string input = "*3";  // no CRLF after the count
    auto r = parse_command(input);

    EXPECT_EQ(r.status, ParseStatus::Incomplete);
}

// ---- The consumed bookkeeping (what rung 1.2 depends on) ----------------

TEST(ParseCommand, TwoCommandsConsumesOnlyTheFirst) {
    const std::string ping = "*1\r\n$4\r\nPING\r\n";
    const std::string set = "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n";
    const std::string input = ping + set;

    auto r = parse_command(input);
    ASSERT_EQ(r.status, ParseStatus::Ok);
    EXPECT_EQ(r.args, (Args{"PING"}));
    EXPECT_EQ(r.consumed, ping.size());  // <-- only the first command's bytes

    // The caller would advance past `consumed`; the remainder parses next.
    auto r2 = parse_command(SV{input}.substr(r.consumed));
    ASSERT_EQ(r2.status, ParseStatus::Ok);
    EXPECT_EQ(r2.args, (Args{"SET", "foo", "bar"}));
}

// ---- Error: malformed (a real protocol violation, not "need more") -------

TEST(ParseCommand, MissingArrayPrefixIsError) {
    const std::string input = "PING\r\n";  // inline command, not a RESP array
    auto r = parse_command(input);
    EXPECT_EQ(r.status, ParseStatus::Error);
}

TEST(ParseCommand, NonNumericArrayCountIsError) {
    const std::string input = "*x\r\n";
    auto r = parse_command(input);
    EXPECT_EQ(r.status, ParseStatus::Error);
}

TEST(ParseCommand, NonNumericBulkLengthIsError) {
    const std::string input = "*1\r\n$x\r\n";
    auto r = parse_command(input);
    EXPECT_EQ(r.status, ParseStatus::Error);
}

TEST(ParseCommand, ElementNotBulkStringIsError) {
    const std::string input = "*1\r\n#4\r\nPING\r\n";  // '#' instead of '$'
    auto r = parse_command(input);
    EXPECT_EQ(r.status, ParseStatus::Error);
}

TEST(ParseCommand, BulkLengthMismatchIsError) {
    // "$3" but the data+terminator don't line up ("SETX" then CRLF).
    const std::string input = "*1\r\n$3\r\nSETX\r\n";
    auto r = parse_command(input);
    EXPECT_EQ(r.status, ParseStatus::Error);
}

// ---- Edge cases ----------------------------------------------------------

TEST(ParseCommand, EmptyArrayIsOk) {
    const std::string input = "*0\r\n";
    auto r = parse_command(input);
    ASSERT_EQ(r.status, ParseStatus::Ok);
    EXPECT_TRUE(r.args.empty());
    EXPECT_EQ(r.consumed, input.size());
}

TEST(ParseCommand, EmptyBulkStringIsOk) {
    const std::string input = "*1\r\n$0\r\n\r\n";
    auto r = parse_command(input);
    ASSERT_EQ(r.status, ParseStatus::Ok);
    EXPECT_EQ(r.args, (Args{""}));
    EXPECT_EQ(r.consumed, input.size());
}
