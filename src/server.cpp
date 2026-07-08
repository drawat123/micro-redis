#include "server.hpp"
#include "dispatcher.hpp"
#include "fd.hpp"
#include "resp.hpp"

#include <arpa/inet.h>  // htons, htonl
#include <fcntl.h>      // fcntl, O_NONBLOCK
#include <netinet/in.h> // sockaddr_in
#include <sys/epoll.h>  // epoll_create1, epoll_ctl, epoll_wait
#include <sys/socket.h> // socket, bind, listen, accept, setsockopt
#include <unistd.h>     // read, write, close

#include <array>
#include <cerrno>
#include <cstring>
#include <format>
#include <iostream>
#include <system_error>
#include <unordered_map>

namespace {

struct Connection {
  Fd fd;
  std::string inbuf;
};

// Turn a failed syscall (which reports errno) into a C++ exception
// carrying the system error message. Cleaner than sprinkling perror()+exit()
// everywhere, and it unwinds the stack so every RAII Fd gets closed on the way
// out.
[[noreturn]] void fail(const char *what) {
  throw std::system_error(errno, std::generic_category(), what);
}

// Put a descriptor into non-blocking mode: read()/write()/accept() then return
// immediately with EAGAIN instead of sleeping when there's nothing to do. This
// is mandatory for an event loop — one blocking call on a slow client would
// freeze every other client sharing the thread.
void set_nonblocking(int fd) {
  int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    fail("fcntl(F_GETFL)");
  }
  if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    fail("fcntl(F_SETFL)");
  }
}

// Create the listening socket: socket → SO_REUSEADDR → non-blocking → bind →
// listen. Returned as an Fd so it closes automatically if anything later
// throws.
Fd make_listener(std::uint16_t port) {
  // Create the server socket (IPv4, TCP, default protocol)
  // Using the global scope resolution operator :: before a system call (like
  // ::socket(), ::bind(), or ::fcntl()) explicitly tells the C++ compiler to
  // look for that function in the global namespace instead of the current local
  // namespace.
  Fd listener(::socket(AF_INET, SOCK_STREAM, 0));
  if (!listener.valid()) {
    fail("socket");
  }

  int opt = 1; // 1 to enable, 0 to disable
  // Configure the socket to reuse the local address/port immediately
  if (::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &opt,
                   sizeof(opt)) < 0) {
    fail("setsockopt(SO_REUSEADDR)");
  }

  set_nonblocking(listener.get());

  // Bind the socket to an IP and Port
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);
  if (::bind(listener.get(), reinterpret_cast<sockaddr *>(&addr),
             sizeof(addr)) < 0) {
    fail("bind");
  }

  // Listen for connections
  if (::listen(listener.get(), SOMAXCONN) < 0) {
    fail("listen");
  }

  return listener;
}

} // namespace

Server::Server(std::uint16_t port) : port_(port) {}

void Server::run() {
  Fd listener = make_listener(port_);

  std::cout << std::format("Server listening on port {}...\n", port_);

  // epoll is the Linux "tell me which of these fds are ready" mechanism — the
  // heart of the reactor pattern. epoll_create1 returns an fd representing the
  // interest set; we wrap it in an Fd so it, too, closes automatically.
  Fd epoll(::epoll_create1(0)); // Returns a brand new epoll descriptor
  if (!epoll.valid()) {
    fail("epoll_create1");
  }

  auto epoll_add = [&](int fd, std::uint32_t events) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd; // Store the descriptor identifier
    if (::epoll_ctl(epoll.get(), EPOLL_CTL_ADD, fd, &ev) < 0) {
      fail("epoll_ctl(ADD)");
    }
  };

  // Watch the listener for "readable" — which, for a listening socket, means
  // "a new connection is waiting to be accepted".
  epoll_add(listener.get(),
            EPOLLIN); // Monitor for reading/incoming connections

  // Every accepted client is owned here. Erasing an entry closes its fd (Fd
  // destructor) — so connection cleanup is automatic and leak-free. In rung 1
  // this map's value grows from a bare Fd into a Connection with a read buffer.
  std::unordered_map<int, Connection> connections;

  std::array<epoll_event, 64> ready{}; // epoll_wait fills this each iteration

  while (true) {
    // Block indefinitely until an event occurs (0% CPU utilization while
    // waiting)
    int num_events =
        epoll_wait(epoll.get(), ready.data(), static_cast<int>(ready.size()),
                   -1 /*wait forever*/);
    if (num_events < 0) {
      if (errno == EINTR) {
        continue; // a signal interrupted us; just retry
      }
      fail("epoll_wait");
    }

    for (int i = 0; i < num_events; ++i) {
      const int current_fd = ready[i].data.fd;

      // Scenario A: New connection coming into our listening socket
      if (current_fd == listener.get()) {
        int client = ::accept(listener.get(), nullptr, nullptr);
        if (client < 0) {
          // Ignore transient errors (empty queue or signal interrupts); try
          // again next event cycle.
          if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            continue;
          }
          fail("accept");
        }

        // Make the new client socket non-blocking
        set_nonblocking(client);

        // Add the new client socket to the epoll watch list
        epoll_add(client,
                  EPOLLIN |
                      EPOLLET); // Using Edge-Triggered mode for performance

        connections.emplace(client,
                            Connection{Fd(client), {}}); // take ownership

        std::cout << "New client accepted on file descriptor: " << client
                  << "\n";
      }
      // Scenario B: An existing client socket has sent data to read
      else if (ready[i].events & EPOLLIN) {
        size_t offset = 0;
        bool close_conn = false;
        auto &conn = connections.at(current_fd);

        while (true) {
          std::array<char, 1024> buf{}; // scratch buffer for read
          ssize_t bytes_read = ::read(current_fd, buf.data(), buf.size());

          if (bytes_read > 0) {
            conn.inbuf.append(buf.data(), bytes_read);

            // Parse loop: execute all complete commands currently in the buffer
            while (true) {
              std::string_view view = conn.inbuf;
              view.remove_prefix(offset);
              if (view.empty()) {
                break;
              }

              ParseResult res = parse_command(view);
              if (res.status == ParseStatus::Incomplete) {
                break; // wait for more bytes from the socket
              }
              if (res.status == ParseStatus::Error) {
                close_conn = true;
                break; // close the connection
              }

              std::string reply = dispatch(res.args, store_);
              ::send(current_fd, reply.data(), reply.length(), MSG_NOSIGNAL);

              offset += res.consumed; // advance past this command
            }

            if (close_conn) {
              break;
            }

          } else {
            // Read returned -1 or 0
            if (bytes_read < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
              break; // Buffer is dry. Not an error, just done reading for now.
            }

            // If it reaches here, the connection is dead (either EOF or a hard
            // error)
            close_conn = true;
            if (bytes_read == 0) {
              std::cout << "Client " << current_fd
                        << " disconnected cleanly.\n";
            } else {
              std::cerr << "Client read error on socket " << current_fd << ": "
                        << std::strerror(errno) << "\n";
            }
            break;
          }
        }

        if (close_conn) {
          ::epoll_ctl(epoll.get(), EPOLL_CTL_DEL, current_fd, nullptr);
          connections.erase(current_fd);
        } else {
          // drop everything consumed; keep the leftover
          conn.inbuf.erase(0, offset);
        }
      }
    }
  }
}