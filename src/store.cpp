#include "store.hpp"

Store::Store(size_t capacity) : max_capacity_(capacity) {
  index_.reserve(capacity);
}

void Store::set(std::string_view key, std::string_view value,
                std::optional<std::chrono::seconds> ttl, TimePoint now) {
  std::optional<TimePoint> expires_at;
  if (ttl) {
    expires_at = now + *ttl;
  }

  std::string strKey = std::string(key);
  Entry entry{std::string(value), expires_at};

  auto idxItr = getEntry(key, now, true); // SET promotes recency
  if (idxItr == index_.end()) {
    if (max_capacity_ != 0 && lru_.size() >= max_capacity_) {
      index_.erase(lru_.back().first);
      lru_.pop_back();
    }

    StrEntryPair pair = std::make_pair(strKey, std::move(entry));
    index_[strKey] = lru_.insert(lru_.begin(), std::move(pair));
  } else {
    idxItr->second->second = std::move(entry);
  }
}

const std::string *Store::get(std::string_view key, TimePoint now) {
  auto idxItr = getEntry(key, now, true); // GET promotes recency
  return idxItr != index_.end() ? &idxItr->second->second.value : nullptr;
}

long Store::ttl(std::string_view key, TimePoint now) {
  auto idxItr = getEntry(key, now, false); // TTL is a peek
  if (idxItr == index_.end()) {
    return -2;
  }

  auto &entry = idxItr->second->second;
  if (!entry.expires_at) {
    return -1;
  }

  auto remaining =
      std::chrono::ceil<std::chrono::seconds>(*entry.expires_at - now);

  return remaining.count();
}

bool Store::expire(std::string_view key, std::chrono::seconds ttl,
                   TimePoint now) {
  auto idxItr = getEntry(key, now, false); // EXPIRE is a peek
  if (idxItr == index_.end()) {
    return false;
  }

  auto &entry = idxItr->second->second;
  entry.expires_at = now + ttl;
  return true;
}

void Store::sweep(TimePoint now) {
  for (auto idxItr = index_.begin(); idxItr != index_.end();) {
    const auto &entry = idxItr->second->second;

    if (entry.expires_at && now >= *entry.expires_at) {
      lru_.erase(idxItr->second);
      idxItr = index_.erase(idxItr);
    } else {
      ++idxItr;
    }
  }
}

bool Store::del(std::string_view key, TimePoint now) {
  auto idxItr = getEntry(key, now, false); // DEL is a peek
  if (idxItr == index_.end()) {
    return false;
  }

  lru_.erase(idxItr->second);
  index_.erase(idxItr);
  return true;
}

bool Store::exists(std::string_view key, TimePoint now) {
  return getEntry(key, now, false) != index_.end(); // EXISTS is a peek
}

std::size_t Store::size() const { return index_.size(); }

Store::IndexIterator Store::getEntry(std::string_view key, TimePoint now,
                                     bool update_lru) {
  auto idxItr = index_.find(key);
  if (idxItr == index_.end()) {
    return index_.end();
  }

  auto &entry = idxItr->second->second;

  if (entry.expires_at && now >= *entry.expires_at) {
    lru_.erase(idxItr->second);
    index_.erase(idxItr);
    return index_.end();
  }

  if (update_lru) {
    lru_.splice(lru_.begin(), lru_, idxItr->second);
  }

  return idxItr;
}