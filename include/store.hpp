#pragma once

#include <chrono>
#include <list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

class Store {
public:
  Store(size_t capacity);

  void set(std::string_view key, std::string_view value,
           std::optional<std::chrono::seconds> ttl, TimePoint now);

  const std::string *get(std::string_view key, TimePoint now); // lazy-expires

  long ttl(std::string_view key, TimePoint now); // -2 / -1 / remaining

  bool expire(std::string_view key, std::chrono::seconds ttl, TimePoint now);

  void sweep(TimePoint now); // active expiration: drop everything expired

  bool del(std::string_view key, TimePoint now);

  bool exists(std::string_view key, TimePoint now);

  std::size_t size() const;

private:
  struct Entry {
    std::string value;
    std::optional<TimePoint> expires_at; // nullopt = never
  };

  struct TransparentHash {
    using is_transparent = void; // ← the opt-in marker; this is the magic
    std::size_t operator()(std::string_view sv) const noexcept {
      return std::hash<std::string_view>{}(sv);
    }
  };

  using StrEntryPair = std::pair<std::string, Entry>;

  using LruIterator = std::list<StrEntryPair>::iterator;

  using Index = std::unordered_map<std::string, LruIterator, TransparentHash,
                                   std::equal_to<>>;
  using IndexIterator = Index::iterator;

  IndexIterator getEntry(std::string_view key, TimePoint now, bool update_lru);

  size_t max_capacity_;

  // front = most-recently-used, back = least-recently-used
  std::list<StrEntryPair> lru_;
  Index index_; // key -> node
};