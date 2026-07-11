// micro-redis — rung 0
#include "config.hpp"
#include "server.hpp"

#include <cstdint>
#include <cstdio>
#include <exception>

// Entry point. Usage:  micro-redis [port]   (default port 6380)
int main(int argc, char **argv) {
  std::uint16_t port = 6380; // 6379 is real Redis; 6380 keeps us clear of it
  std::size_t maxkeys = 100;

  for (int i = 1; i < argc; ++i) {
    std::string_view arg = argv[i];
    if (arg == "--maxkeys" && i + 1 < argc) {
      auto parsed = parse_size(argv[++i]);
      if (!parsed) {
        std::fprintf(stderr, "invalid maxkeys: %s\n", argv[i]);
        return 1;
      }
      maxkeys = *parsed;
    } else {
      auto parsed = parse_port(arg);
      if (!parsed) {
        std::fprintf(stderr, "invalid port: %s (expected 1-65535)\n", argv[i]);
        return 1;
      }
      port = *parsed;
    }
  }

  std::printf(
      "micro-redis server listening on port %u (maxkeys: %zu) — Ctrl-C to stop\n",
      port, maxkeys);
  std::fflush(stdout);

  try {
    Server server(port, maxkeys);
    server.run();
  } catch (const std::exception &e) {
    std::fprintf(stderr, "fatal: %s\n", e.what());
    return 1;
  }
  return 0;
}
