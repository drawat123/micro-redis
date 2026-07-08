#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

class Store {
public:
  void set(std::string_view key, std::string_view value);

  const std::string* get(std::string_view key) const;

  bool del(std::string_view key);

  bool exists(std::string_view key) const;

private:
  std::unordered_map<std::string, std::string> store_;
};