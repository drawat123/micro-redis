#pragma once

#include <unistd.h>  // ::close
#include <utility>   // std::exchange

// RAII wrapper around a raw file descriptor (a socket, epoll instance, etc.).
//
// In C, you must remember to ::close(fd) on every code path — including error
// paths and early returns. Forget one and you leak a descriptor (the C++/Linux
// equivalent of a HANDLE leak on Windows). This class makes the compiler do it
// for you: the destructor closes the fd, so it's released the moment the Fd
// goes out of scope. This is "the rule of zero" in action — own the resource in
// a small type, and everything that holds an Fd becomes leak-safe automatically.
//
// It is move-only: a descriptor has exactly one owner. Copying would mean two
// owners closing the same fd (a double-close bug), so copies are deleted.
class Fd {
public:
    Fd() = default;
    explicit Fd(int fd) noexcept : fd_(fd) {}

    ~Fd() { reset(); }

    // No copying (single ownership).
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;

    // Moving transfers ownership; the source is left empty (-1).
    Fd(Fd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    Fd& operator=(Fd&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }

    // Give up ownership without closing (caller becomes responsible).
    int release() noexcept { return std::exchange(fd_, -1); }

private:
    void reset() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    int fd_ = -1;
};
