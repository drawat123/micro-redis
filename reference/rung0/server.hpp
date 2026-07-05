#pragma once

#include <cstdint>

// Rung 0 of micro-redis: a non-blocking TCP echo server built on epoll.
//
// It accepts many simultaneous connections on a single thread and echoes back
// whatever each client sends. There is no protocol and no key-value store yet —
// this rung exists to make the event loop (accept → read → write, driven by
// epoll) real and correct before we add meaning to the bytes in rung 1.
//
// See docs/PRIMER.md §4 for the event-loop model this implements.
class EchoServer {
public:
    explicit EchoServer(std::uint16_t port);

    // Runs the event loop forever (until the process is killed).
    // Throws std::system_error if a syscall fails during setup or the loop.
    void run();

private:
    std::uint16_t port_;
};
