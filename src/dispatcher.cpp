#include "dispatcher.hpp"
#include <cctype>
#include <string>

// Helper to convert command to uppercase
static std::string to_upper(std::string_view sv) {
  std::string res;
  res.reserve(sv.size());
  for (char c : sv) {
    res.push_back(std::toupper(static_cast<unsigned char>(c)));
  }
  return res;
}

std::string dispatch(const std::vector<std::string_view> &args, Store &store) {
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
    if (args.size() != 3) {
      return "-ERR wrong number of arguments for 'SET' command\r\n";
    }
    store.set(args[1], args[2]);
    return "+OK\r\n";
  }

  if (cmd == "GET") {
    if (args.size() != 2) return "-ERR wrong number of arguments for 'GET' command\r\n";
    const std::string* val = store.get(args[1]);
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
    bool deleted = store.del(args[1]);
    return ":" + std::to_string(deleted ? 1 : 0) + "\r\n";
  }

  if (cmd == "EXISTS") {
    if (args.size() != 2) {
      return "-ERR wrong number of arguments for 'EXISTS' command\r\n";
    }
    bool exists = store.exists(args[1]);
    return ":" + std::to_string(exists ? 1 : 0) + "\r\n";
  }

  return "-ERR unknown command '" + std::string(args[0]) + "'\r\n";
}
