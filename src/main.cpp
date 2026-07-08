// micro-redis — rung 0
#include "config.hpp"
#include "server.hpp"

#include <cstdint>
#include <cstdio>
#include <exception>

// Entry point. Usage:  micro-redis [port]   (default port 6380)
int main(int argc, char **argv) {
  std::uint16_t port = 6380; // 6379 is real Redis; 6380 keeps us clear of it

  if (argc > 1) {
    auto parsed = parse_port(argv[1]);
    if (!parsed) {
      std::fprintf(stderr, "invalid port: %s (expected 1-65535)\n", argv[1]);
      return 1;
    }
    port = *parsed;
  }

  std::printf(
      "micro-redis rung-0 (echo) listening on port %u — Ctrl-C to stop\n",
      port);
  std::fflush(stdout);

  try {
    Server server(port);
    server.run();
  } catch (const std::exception &e) {
    std::fprintf(stderr, "fatal: %s\n", e.what());
    return 1;
  }
  return 0;
}
