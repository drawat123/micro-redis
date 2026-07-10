#pragma once

#include <chrono>
#include <string>
#include <string_view>
#include <unordered_map>

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

class Store {
public:
  void set(std::string_view key, std::string_view value,
           std::optional<std::chrono::seconds> ttl, TimePoint now);

  const std::string *get(std::string_view key, TimePoint now); // lazy-expires

  long ttl(std::string_view key, TimePoint now); // -2 / -1 / remaining

  bool expire(std::string_view key, std::chrono::seconds ttl, TimePoint now);

  void sweep(TimePoint now); // active expiration: drop everything expired

  bool del(std::string_view key, TimePoint now);

  bool exists(std::string_view key, TimePoint now);

private:
  struct Entry {
    std::string value;
    std::optional<TimePoint> expires_at; // nullopt = never
  };

  Entry *getEntry(std::string_view key, TimePoint now);

  std::unordered_map<std::string, Entry> store_;
};