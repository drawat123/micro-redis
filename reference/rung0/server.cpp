#include "server.hpp"

#include "fd.hpp"

#include <arpa/inet.h>    // htons, htonl
#include <fcntl.h>        // fcntl, O_NONBLOCK
#include <netinet/in.h>   // sockaddr_in
#include <sys/epoll.h>    // epoll_create1, epoll_ctl, epoll_wait
#include <sys/socket.h>   // socket, bind, listen, accept, setsockopt
#include <unistd.h>       // read, write, close

#include <array>
#include <cerrno>
#include <cstring>
#include <system_error>
#include <unordered_map>

namespace {

// Turn a failed syscall (which reports errno) into a C++ exception carrying the
// system error message. Cleaner than sprinkling perror()+exit() everywhere, and
// it unwinds the stack so every RAII Fd gets closed on the way out.
[[noreturn]] void fail(const char* what) {
    throw std::system_error(errno, std::generic_category(), what);
}

// Put a descriptor into non-blocking mode: read()/write()/accept() then return
// immediately with EAGAIN instead of sleeping when there's nothing to do. This
// is mandatory for an event loop — one blocking call on a slow client would
// freeze every other client sharing the thread.
void set_nonblocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) fail("fcntl(F_GETFL)");
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) fail("fcntl(F_SETFL)");
}

// Create the listening socket: socket → SO_REUSEADDR → non-blocking → bind →
// listen. Returned as an Fd so it closes automatically if anything later throws.
Fd make_listener(std::uint16_t port) {
    Fd listener(::socket(AF_INET, SOCK_STREAM, 0));
    if (!listener.valid()) fail("socket");

    // SO_REUSEADDR lets us re-bind the port immediately after a restart instead
    // of waiting out the kernel's TIME_WAIT window (you hit this concept in the
    // Java TCP work). Standard for servers.
    int yes = 1;
    if (::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
        fail("setsockopt(SO_REUSEADDR)");

    set_nonblocking(listener.get());

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);  // listen on all interfaces
    addr.sin_port = htons(port);               // host→network byte order
    if (::bind(listener.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        fail("bind");

    if (::listen(listener.get(), SOMAXCONN) < 0) fail("listen");

    return listener;
}

}  // namespace

EchoServer::EchoServer(std::uint16_t port) : port_(port) {}

void EchoServer::run() {
    Fd listener = make_listener(port_);

    // epoll is the Linux "tell me which of these fds are ready" mechanism — the
    // heart of the reactor pattern. epoll_create1 returns an fd representing the
    // interest set; we wrap it in an Fd so it, too, closes automatically.
    Fd epoll(::epoll_create1(0));
    if (!epoll.valid()) fail("epoll_create1");

    auto epoll_add = [&](int fd, std::uint32_t events) {
        epoll_event ev{};
        ev.events = events;
        ev.data.fd = fd;
        if (::epoll_ctl(epoll.get(), EPOLL_CTL_ADD, fd, &ev) < 0) fail("epoll_ctl(ADD)");
    };

    // Watch the listener for "readable" — which, for a listening socket, means
    // "a new connection is waiting to be accepted".
    epoll_add(listener.get(), EPOLLIN);

    // Every accepted client is owned here. Erasing an entry closes its fd (Fd
    // destructor) — so connection cleanup is automatic and leak-free. In rung 1
    // this map's value grows from a bare Fd into a Connection with a read buffer.
    std::unordered_map<int, Fd> connections;

    std::array<epoll_event, 64> ready{};  // epoll_wait fills this each iteration
    std::array<char, 4096> buf{};         // scratch buffer for read/echo

    for (;;) {
        int n = ::epoll_wait(epoll.get(), ready.data(),
                             static_cast<int>(ready.size()), -1 /* wait forever */);
        if (n < 0) {
            if (errno == EINTR) continue;  // a signal interrupted us; just retry
            fail("epoll_wait");
        }

        for (int i = 0; i < n; ++i) {
            const int fd = ready[i].data.fd;

            // --- Case 1: the listener is ready → accept new connection(s). ---
            if (fd == listener.get()) {
                for (;;) {  // drain the accept queue in one go
                    int client = ::accept(listener.get(), nullptr, nullptr);
                    if (client < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;  // no more waiting
                        if (errno == EINTR) continue;
                        fail("accept");
                    }
                    set_nonblocking(client);
                    epoll_add(client, EPOLLIN);
                    connections.emplace(client, Fd(client));  // take ownership
                }
                continue;
            }

            // --- Case 2: a client socket is ready → read, then echo back. ---
            bool close_it = false;
            for (;;) {
                ssize_t got = ::read(fd, buf.data(), buf.size());
                if (got > 0) {
                    // Echo the bytes straight back. Simplified for rung 0: we
                    // assume the kernel's send buffer has room. A production
                    // server would handle a short/EAGAIN write by buffering the
                    // remainder and waiting for EPOLLOUT — we add that when the
                    // protocol arrives.
                    ssize_t off = 0;
                    while (off < got) {
                        ssize_t put = ::write(fd, buf.data() + off,
                                              static_cast<size_t>(got - off));
                        if (put < 0) {
                            if (errno == EINTR) continue;
                            close_it = true;  // client gone / write error
                            break;
                        }
                        off += put;
                    }
                    if (close_it) break;
                } else if (got == 0) {
                    close_it = true;  // peer performed an orderly shutdown
                    break;
                } else {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;  // fully drained
                    if (errno == EINTR) continue;
                    close_it = true;  // real read error
                    break;
                }
            }

            if (close_it) {
                // Remove from epoll first, then drop from the map (which closes
                // the fd via Fd's destructor).
                ::epoll_ctl(epoll.get(), EPOLL_CTL_DEL, fd, nullptr);
                connections.erase(fd);
            }
        }
    }
}
