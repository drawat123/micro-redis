#include "resp.hpp"
#include "config.hpp"
#include <cstddef>
#include <string_view>

namespace {

constexpr std::size_t kMaxBulkLength = 10000;

ParseStatus parseArrayHeader(std::string_view input, std::size_t &start,
                             std::size_t &arraySize) {
  if (start >= input.size()) {
    return ParseStatus::Incomplete;
  }
  if (input[start++] != '*') {
    return ParseStatus::Error;
  }

  std::size_t end = input.find("\r\n", start);
  if (end == std::string_view::npos) {
    return ParseStatus::Incomplete;
  }

  auto opt = parse_size(input.substr(start, end - start));
  if (!opt) {
    return ParseStatus::Error;
  }

  arraySize = opt.value();
  if (arraySize > kMaxBulkLength) {
    return ParseStatus::Error;
  }

  start = end + 2; // Skip "\r\n"

  return ParseStatus::Ok;
}

ParseStatus parseBulkStringHeader(std::string_view input, std::size_t &start,
                                  std::size_t &elementSize) {
  if (start >= input.size()) {
    return ParseStatus::Incomplete;
  }
  if (input[start++] != '$') {
    return ParseStatus::Error;
  }

  std::size_t end = input.find("\r\n", start);
  if (end == std::string_view::npos) {
    return ParseStatus::Incomplete;
  }

  auto opt = parse_size(input.substr(start, end - start));
  if (!opt) {
    return ParseStatus::Error;
  }

  elementSize = opt.value();
  if (elementSize > kMaxBulkLength) {
    return ParseStatus::Error;
  }

  start = end + 2;

  return ParseStatus::Ok;
}

ParseStatus parseBulkString(std::string_view input, std::size_t &start,
                            std::size_t elementSize, std::string_view &arg) {
  if (start + elementSize + 2 > input.size()) {
    return ParseStatus::Incomplete;
  }

  arg = input.substr(start, elementSize);
  start += elementSize;

  if (input.substr(start, 2) != "\r\n") {
    return ParseStatus::Error;
  }

  start += 2;
  return ParseStatus::Ok;
}

} // namespace — file-private helpers end here.

// Public API (external linkage): must live OUTSIDE the anonymous namespace so
// server.cpp and the tests can link against it.
ParseResult parse_command(std::string_view input) {
  ParseResult res{ParseStatus::Incomplete, {}, 0};
  std::size_t start = 0, arraySize = 0;

  ParseStatus status = parseArrayHeader(input, start, arraySize);
  if (status != ParseStatus::Ok) {
    res.status = status;
    return res;
  }

  for (std::size_t i = 0; i < arraySize; i++) {
    std::size_t elementSize = 0;

    status = parseBulkStringHeader(input, start, elementSize);
    if (status != ParseStatus::Ok) {
      res.status = status;
      return res;
    }

    std::string_view arg;
    status = parseBulkString(input, start, elementSize, arg);
    if (status != ParseStatus::Ok) {
      res.status = status;
      return res;
    }

    res.args.push_back(arg);
  }

  res.status = ParseStatus::Ok;
  res.consumed = start;

  return res;
}