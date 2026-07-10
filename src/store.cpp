#include "store.hpp"

void Store::set(std::string_view key, std::string_view value,
                std::optional<std::chrono::seconds> ttl, TimePoint now) {

  std::optional<TimePoint> expires_at;

  if (ttl) {
    expires_at = now + *ttl;
  }

  Entry entry{std::string(value), expires_at};

  store_[std::string(key)] = std::move(entry);
}

const std::string *Store::get(std::string_view key, TimePoint now) {
  auto *entry = getEntry(key, now);
  return entry ? &entry->value : nullptr;
}

long Store::ttl(std::string_view key, TimePoint now) {
  Entry *entry = getEntry(key, now);
  if (!entry) {
    return -2;
  }

  if (!entry->expires_at) {
    return -1;
  }

  auto remaining = std::chrono::ceil<std::chrono::seconds>(*entry->expires_at - now);

  return remaining.count();
}

bool Store::expire(std::string_view key, std::chrono::seconds ttl,
                   TimePoint now) {
  Entry *entry = getEntry(key, now);
  if (!entry) {
    return false;
  }

  entry->expires_at = now + ttl;
  return true;
}

void Store::sweep(TimePoint now) {
  for (auto it = store_.begin(); it != store_.end();) {
    const auto &entry = it->second;

    if (entry.expires_at && now >= *entry.expires_at) {
      it = store_.erase(it);
    } else {
      ++it;
    }
  }
}

bool Store::del(std::string_view key, TimePoint now) {
  if (!getEntry(key, now)) {
    return false;
  }
  return store_.erase(std::string(key)) > 0;
}

bool Store::exists(std::string_view key, TimePoint now) {
  return getEntry(key, now) != nullptr;
}

Store::Entry *Store::getEntry(std::string_view key, TimePoint now) {
  auto it = store_.find(std::string(key));
  if (it == store_.end()) {
    return nullptr;
  }

  auto &entry = it->second;

  if (entry.expires_at && now >= *entry.expires_at) {
    store_.erase(it);
    return nullptr;
  }

  return &entry;
}