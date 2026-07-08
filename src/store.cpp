#include "store.hpp"

void Store::set(std::string_view key, std::string_view value) {
  store_[std::string(key)] = std::string(value);
}

const std::string* Store::get(std::string_view key) const {
  auto it = store_.find(std::string(key));
  if (it != store_.end()) {
    return &it->second;
  }
  return nullptr;
}

bool Store::del(std::string_view key) {
  return store_.erase(std::string(key)) > 0;
}

bool Store::exists(std::string_view key) const {
  return store_.find(std::string(key)) != store_.end();
}