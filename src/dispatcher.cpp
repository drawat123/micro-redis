#include "dispatcher.hpp"
#include "config.hpp"
#include <cctype>
#include <chrono>
#include <string>

using namespace std::chrono_literals;

// Helper to convert command to uppercase
static std::string to_upper(std::string_view sv) {
  std::string res;
  res.reserve(sv.size());
  for (char c : sv) {
    res.push_back(std::toupper(static_cast<unsigned char>(c)));
  }
  return res;
}

std::string dispatch(const std::vector<std::string_view> &args, Store &store,
                     TimePoint now) {
  if (args.empty()) {
    return "-ERR empty command\r\n";
  }

  std::string cmd = to_upper(args[0]);

  if (cmd == "PING") {
    if (args.size() != 1) {
      return "-ERR wrong number of arguments for 'PING' command\r\n";
    }

    return "+PONG\r\n";
  }

  if (cmd == "SET") {
    if (args.size() == 3) {
      store.set(args[1], args[2], std::nullopt, now);
    } else if (args.size() == 5 && args[3] == "EX") {
      auto opt = parse_size(args[4]);
      if (!opt || *opt == 0) {
        return "-ERR invalid expire time\r\n";
      }
      store.set(args[1], args[2], std::chrono::seconds{*opt}, now);
    } else {
      return "-ERR wrong number of arguments for 'SET' command\r\n";
    }

    return "+OK\r\n";
  }

  if (cmd == "GET") {
    if (args.size() != 2) {
      return "-ERR wrong number of arguments for 'GET' command\r\n";
    }

    const std::string *val = store.get(args[1], now);
    if (!val) {
      return "$-1\r\n";
    }

    // format as bulk string
    return "$" + std::to_string(val->size()) + "\r\n" + *val + "\r\n";
  }

  if (cmd == "DEL") {
    if (args.size() != 2) {
      return "-ERR wrong number of arguments for 'DEL' command\r\n";
    }

    bool deleted = store.del(args[1], now);
    return ":" + std::to_string(deleted ? 1 : 0) + "\r\n";
  }

  if (cmd == "EXISTS") {
    if (args.size() != 2) {
      return "-ERR wrong number of arguments for 'EXISTS' command\r\n";
    }
    bool exists = store.exists(args[1], now);
    return ":" + std::to_string(exists ? 1 : 0) + "\r\n";
  }

  if (cmd == "TTL") {
    if (args.size() != 2) {
      return "-ERR wrong number of arguments for 'TTL' command\r\n";
    }

    int ttl = store.ttl(args[1], now);
    return ":" + std::to_string(ttl) + "\r\n";
  }

  if (cmd == "EXPIRE") {
    if (args.size() != 3) {
      return "-ERR wrong number of arguments for 'EXPIRE' command\r\n";
    }

    auto opt = parse_size(args[2]);
    if (!opt || *opt == 0) {
      return "-ERR invalid expire time\r\n";
    }

    bool set = store.expire(args[1], std::chrono::seconds{*opt}, now);
    return ":" + std::to_string(set ? 1 : 0) + "\r\n";
  }

  if (cmd == "DBSIZE") {
    if (args.size() != 1) {
      return "-ERR wrong number of arguments for 'DBSIZE' command\r\n";
    }
    return ":" + std::to_string(store.size()) + "\r\n";
  }

  return "-ERR unknown command '" + std::string(args[0]) + "'\r\n";
}
