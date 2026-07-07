#pragma once

#include <string_view>
#include <vector>

enum class ParseStatus { Ok, Incomplete, Error };

struct ParseResult {
  ParseStatus status;
  std::vector<std::string_view> args; // ["SET","foo","bar"]
  size_t consumed; // bytes used, so caller can advance its buffer
};

ParseResult parse_command(std::string_view input);