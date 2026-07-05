// micro-redis — rung 0

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <iostream>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

// Helper utility to make any socket non-blocking
bool make_non_blocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1)
    return false;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

int main() {
  // Create the server socket (IPv4, TCP, default protocol)
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd == -1) {
    std::cerr << "Failed to create socket\n";
    return 1;
  }

  const uint16_t port = 6380;

  // Bind the socket to an IP and Port
  sockaddr_in server_addr{};
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(port);

  int opt = 1; // 1 to enable, 0 to disable
  // Configure the socket to reuse the local address/port immediately
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    std::cerr << "setsockopt(SO_REUSEADDR) failed: " << std::strerror(errno)
              << "\n";
    close(server_fd);
    return 1;
  }

  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    std::cerr << "Bind failed, errcode: " << errno << " ("
              << std::strerror(errno) << ")\n";
    close(server_fd);
    return 1;
  }

  // Listen for connections
  if (listen(server_fd, SOMAXCONN) < 0) {
    std::cerr << "Listen failed, errcode: " << errno << " ("
              << std::strerror(errno) << ")\n";
    close(server_fd);
    return 1;
  }

  std::cout << std::format("Server listening on port {}...\n", port);

  // CREATE THE EPOLL INSTANCE
  int epoll_fd = epoll_create1(0); // Returns a brand new epoll descriptor
  if (epoll_fd < 0) {
    std::cerr << "epoll creation failed, errcode: " << errno << " ("
              << std::strerror(errno) << ")\n";
    close(server_fd);
    return 1;
  }

  // SET SERVER SOCKET TO NON-BLOCKING & ADD TO WATCH LIST
  if (!make_non_blocking(server_fd)) {
    std::cerr << "Failed to set server socket to non-blocking\n";
    close(epoll_fd);
    close(server_fd);
    return 1;
  }

  struct epoll_event ev {};
  ev.events = EPOLLIN;    // Monitor for reading/incoming connections
  ev.data.fd = server_fd; // Store the descriptor identifier

  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) < 0) {
    std::cerr << "Failed to add server_fd to epoll\n";
    close(epoll_fd);
    close(server_fd);
    return 1;
  }

  const int MAX_EVENTS = 10;
  struct epoll_event events[MAX_EVENTS];

  while (true) {
    // Block indefinitely until an event occurs (0% CPU utilization while
    // waiting)
    int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
    if (num_events < 0) {
      std::cerr << "epoll_wait runtime error\n";
      break;
    }

    for (int i = 0; i < num_events; ++i) {
      int current_fd = events[i].data.fd;

      // Scenario A: New connection coming into our listening socket
      if (current_fd == server_fd) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int client_fd =
            accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
          std::cerr << "Accept tracking error\n";
          continue;
        }

        // Make the new client socket non-blocking
        make_non_blocking(client_fd);

        // Add the new client socket to the epoll watch list
        struct epoll_event client_ev {};
        client_ev.events =
            EPOLLIN | EPOLLET; // Using Edge-Triggered mode for performance
        client_ev.data.fd = client_fd;

        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev) < 0) {
          std::cerr << "Failed to add client to epoll\n";
          close(client_fd);
        } else {
          std::cout << "New client accepted on file descriptor: " << client_fd
                    << "\n";
        }
      }
      // Scenario B: An existing client socket has sent data to read
      else if (events[i].events & EPOLLIN) {
        char buffer[1024];

        // Because we used Edge-Triggered (EPOLLET), we MUST loop and read until
        // empty
        while (true) {
          ssize_t bytes_read = read(current_fd, buffer, sizeof(buffer));

          if (bytes_read > 0) {
            // Successfully processed data payload chunks
            std::cout << "Read " << bytes_read << " bytes from client "
                      << current_fd << "\n";
            std::cout.write(buffer, bytes_read);
            // Echo the data back to client as a basic placeholder action
            send(current_fd, buffer, bytes_read, MSG_NOSIGNAL);
          } else {
            // Read returned -1 or 0
            if (bytes_read < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
              break; // Buffer is dry. Not an error, just done reading for now.
            }

            // If it reaches here, the connection is dead (either EOF or a hard
            // error)
            if (bytes_read == 0) {
              std::cout << "Client " << current_fd
                        << " disconnected cleanly.\n";
            } else {
              std::cerr << "Client read error on socket " << current_fd << ": "
                        << std::strerror(errno) << "\n";
            }

            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, current_fd, nullptr);
            close(current_fd);
            break;
          }
        }
      }
    }
  }

  // 7. Cleanup and close the file descriptors
  close(epoll_fd);
  close(server_fd);

  return 0;
}
