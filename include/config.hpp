#pragma once

#include <charconv> // std::from_chars
#include <cstdint>
#include <optional>
#include <string_view>

// Parse a TCP port from a string (e.g. a command-line argument).
//
// Returns std::nullopt for anything that isn't a whole number in [1, 65535].
// This is pure, allocation-free logic with no I/O — which is exactly why it
// lives in its own header and gets a unit test (see tests/test_config.cpp).
// The networking code is hard to unit-test; small pure helpers like this are
// where your test suite earns its keep.
//
// std::from_chars is the modern, locale-independent, non-throwing way to parse
// numbers (unlike std::stoi, which throws, or atoi, which can't report errors).
inline std::optional<std::uint16_t> parse_port(std::string_view s) {
  int value = 0;
  const char *begin = s.data();
  const char *end = s.data() + s.size();

  auto [ptr, ec] = std::from_chars(begin, end, value);
  if (ec != std::errc{} || ptr != end) {
    return std::nullopt; // not a number, or trailing junk like "123abc"
  }
  if (value < 1 || value > 65535) {
    return std::nullopt; // out of the valid TCP port range
  }
  return static_cast<std::uint16_t>(value);
}

inline std::optional<std::size_t> parse_size(std::string_view s) {
  std::size_t value{};
  const char *begin = s.data();
  const char *end = begin + s.size();

  auto [ptr, ec] = std::from_chars(begin, end, value);
  if (ec != std::errc{} || ptr != end) {
    return std::nullopt;
  }

  return value;
}
